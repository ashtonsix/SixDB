#include "implementation_loom.h"
#include <ikea/streampack.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <string_view>
#include <thread>

namespace sp = ikea::streampack;
namespace oa = orbital::allocation;
namespace lc = streampack_implementation::loom_consumer;
using Clock = std::chrono::steady_clock;

namespace {
[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "StreamPack Loom check: %s\n", message);
    std::abort();
}
void require(bool condition, const char* message) { if (!condition) fail(message); }
template <class Result> auto take(Result&& result, const char* message) {
    require(bool(result), message);
    return std::move(*result);
}
std::uint64_t ns(Clock::duration elapsed) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
}
std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9;
    x ^= x >> 27; x *= 0x94d049bb133111eb;
    return x ^ (x >> 31);
}
unsigned rounded(unsigned n, unsigned alignment) { return (n + alignment - 1) / alignment * alignment; }

struct Runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    std::uint64_t setup_ns{};
    explicit Runtime(std::uint64_t budget) {
        const auto begin = Clock::now();
        require(bool(loom::adopt(worker)), "adopt Worker");
        arena = take(oa::open_arena(budget), "open Arena");
        require(bool(loom::configure(worker, arena, 0)), "configure zero-Task Worker");
        setup_ns = ns(Clock::now() - begin);
    }
    ~Runtime() {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (true) {
            const auto progress = loom::shutdown(worker);
            if (progress.kind() == orbital::Progress::Kind::done) break;
            require(progress.kind() != orbital::Progress::Kind::failed, "shutdown failed");
            require(Clock::now() < deadline, "shutdown did not drain actual storage uses");
            loom::poll(worker, {256, 0});
            std::this_thread::yield();
        }
        require(!orbital::worker::pending(worker.lane), "physical Messages after shutdown");
        require(arena->charged_bytes.load() == 0, "Arena charge after shutdown");
        loom::relinquish(worker);
    }
};

// Application facts are generated before encoding. The expected consumer never
// calls locate/value, decodes StreamPack metadata or reads the encoded payload.
struct Row {
    std::uint64_t key{};
    unsigned length{}, reserved{}, successor{};
    bool retired{};
};
struct Block {
    oa::Allocation allocation;
    oa::Hold hold;
    sp::storage<sp::byte> windows;
    std::vector<Row> rows;
};
struct Population {
    const sp::layout layout;
    std::vector<Block> blocks;
    std::vector<sp::const_view> views;
    std::uint64_t setup_ns{}, payload_bytes{}, directory_bytes{}, lengths_bytes{}, control_bytes{};
    std::uint64_t selected_payload_bytes{}, reserved_payload_bytes{}, allocation_bytes{}, oracle_bytes{};
    unsigned unaligned_tails{};

    Population(loom::Worker& worker, sp::layout::options d, unsigned block_count, unsigned assigned)
        : layout(take(sp::layout::make(d), "prepare shared layout")) {
        require(block_count && assigned <= sp::slot_capacity, "population dimensions");
        const auto begin = Clock::now();
        blocks.reserve(block_count); views.reserve(block_count);
        for (unsigned b = 0; b < block_count; ++b) {
            auto& block = blocks.emplace_back();
            std::array<unsigned, 4> sizes{d.byte_limit, layout.directory_bytes(),
                layout.lengths_bytes(), layout.control_bytes()};
            std::array<unsigned, 4> offsets{};
            unsigned extent = 0;
            for (unsigned p = 0; p < 4; ++p) {
                extent = rounded(extent, p == 2 ? std::max(64u, layout.lengths_alignment()) : 64);
                offsets[p] = extent; extent += sizes[p];
            }
            block.allocation = take(loom::allocate(worker, {extent, 0, 64, 0,
                oa::Initialization::zero}), "allocate four-plane block from Worker Pool");
            allocation_bytes += block.allocation.size;
            std::array<std::span<sp::byte>, 4> planes;
            for (unsigned p = 0; p < 4; ++p)
                planes[p] = {block.allocation.address + offsets[p], sizes[p]};
            block.windows = {planes[0], planes[1], planes[2], planes[3]};
            auto writable = take(sp::view::attach(layout, block.windows), "bind aligned disjoint windows");
            std::array<ikea::journal::entry, 16> records; ikea::journal effects{records};
            require(bool(sp::initialize(writable, effects)), "checked initialize");
            block.rows.resize(assigned);
            std::vector<unsigned> order(assigned);
            std::iota(order.begin(), order.end(), 0);
            auto random = mix(b + 0x84adf18cb425ec77);
            for (unsigned i = assigned; i > 1; --i) {
                random = mix(random); std::swap(order[i - 1], order[random % i]);
            }
            for (unsigned i = 0; i < assigned; ++i)
                block.rows[order[i]].successor = order[(i + 1) % assigned];
            unsigned payload_offset = 0;
            for (unsigned r = 0; r < assigned; ++r) {
                auto& row = block.rows[r];
                const auto h = mix((std::uint64_t(b) << 32) | r);
                const bool tiny = layout.offset_bytes() == 1;
                row.key = mix(h ^ 0xf261ad5b43814e19);
                row.length = tiny ? 12 + (r + b) % 3 :
                    (layout.width_bytes() == 2 ? 257u : 12u) + h % 31;
                if (d.fixed_reservation) {
                    require(*d.fixed_reservation >= 12, "fixed fixture supports application header/link");
                    row.reserved = *d.fixed_reservation;
                    row.length = d.full ? row.reserved : 12 + unsigned(h % (row.reserved - 11));
                } else row.reserved = row.length + (d.full ? 0u : tiny ? 1u : 3u + unsigned(h % 7));
                require(row.reserved <= d.maximum, "application reservation within schema");
                auto bytes = encode(row, b, r);
                effects.used = 0;
                require(bool(sp::append(writable, bytes, row.reserved, effects)), "checked append");
                selected_payload_bytes += row.length;
                reserved_payload_bytes += row.reserved;
                unaligned_tails += (reinterpret_cast<std::uintptr_t>(planes[0].data()) +
                    payload_offset + row.length - 4) % 4 != 0;
                payload_offset += row.reserved;
            }
            require(bool(writable.validate()), "validate initialized encoded contents");
            require(writable.size() == assigned && writable.extent() == payload_offset,
                "application assigned/extent facts");
            block.hold = take(loom::hold(block.allocation, {0, block.allocation.size}), "retain physical block");
            views.push_back(writable.as_const());
            payload_bytes += sizes[0]; directory_bytes += sizes[1];
            lengths_bytes += sizes[2]; control_bytes += sizes[3];
            oracle_bytes += block.rows.capacity() * sizeof(Row);
        }
        setup_ns = ns(Clock::now() - begin);
    }
    static std::vector<sp::byte> encode(const Row& row, unsigned source, unsigned rank) {
        std::vector<sp::byte> bytes(row.length);
        for (unsigned i = 0; i < row.length; ++i) bytes[i] = sp::byte((source * 29 + rank * 7 + i) % 256);
        if (row.length >= 8) std::memcpy(bytes.data(), &row.key, 8);
        if (row.length >= 12) std::memcpy(bytes.data() + row.length - 4, &row.successor, 4);
        return bytes;
    }
    void change(unsigned source, unsigned rank, unsigned length, unsigned successor) {
        auto& block = blocks[source]; auto& row = block.rows[rank];
        row.length = length; row.successor = successor;
        auto writable = take(sp::view::attach(layout, block.windows), "bind mutation source");
        auto bytes = encode(row, source, rank); std::array<ikea::journal::entry, 16> records; ikea::journal effects{records};
        require(bool(sp::replace(writable, rank, bytes, effects)), "checked fixture replacement");
        require(bool(writable.validate()), "validate changed contents");
    }
    void retire(unsigned source, unsigned rank) {
        auto& block = blocks[source]; auto writable = take(sp::view::attach(layout, block.windows), "bind retirement");
        std::array<ikea::journal::entry, 16> records; ikea::journal effects{records};
        require(bool(sp::retire(writable, rank, effects)), "checked fixture retirement");
        block.rows[rank].retired = true;
        require(bool(writable.validate()), "validate retired contents");
    }
    void release_allocations() {
        // The Holds retain every borrowed byte even after all mutable owners end.
        for (auto& block : blocks) oa::release(block.allocation);
    }
    void release_oracle() {
        for (auto& block : blocks) std::vector<Row>().swap(block.rows);
    }
    std::uint64_t checksum() const {
        std::uint64_t h = 0xcbf29ce484222325;
        for (const auto& view : views)
            for (auto plane : view.windows().planes())
                for (auto byte : plane) { h ^= std::to_integer<unsigned>(byte); h *= 0x100000001b3; }
        return h;
    }
    lc::Outcome expected(lc::Request request) const {
        lc::Outcome out{request.identity * 0x9e3779b97f4a7c15, request.rank, 0, 1, lc::Code::ok};
        for (unsigned step = 0; step < request.steps; ++step) {
            const auto& rows = blocks[request.source].rows;
            if (out.rank >= rows.size()) { out.code = lc::Code::rank; break; }
            const auto& row = rows[out.rank];
            if (row.retired) { out.code = lc::Code::retired; break; }
            if (row.length < 12) { out.code = lc::Code::short_value; break; }
            out.digest = std::rotl(out.digest, 9) ^ row.key ^
                (std::uint64_t(row.length) * 0x94d049bb133111eb) ^ out.rank;
            ++out.consumed; out.rank = row.successor;
            if (out.rank >= rows.size()) { out.code = lc::Code::invalid_link; break; }
        }
        return out;
    }
    std::vector<lc::Outcome> expected(std::span<const lc::Request> requests) const {
        std::vector<lc::Outcome> result; result.reserve(requests.size());
        for (auto request : requests) result.push_back(expected(request));
        return result;
    }
};

std::vector<lc::Request> requests(const Population& population, unsigned count, bool benchmark = false) {
    std::vector<lc::Request> result; result.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const auto source = benchmark ? unsigned(mix(i) % population.blocks.size()) :
            unsigned((i / 3) % population.blocks.size());
        const auto assigned = population.blocks[source].rows.size();
        const auto rank = benchmark ? unsigned(mix(i + 3191) % assigned) : unsigned((i % 7) * 17 % assigned);
        result.push_back({source, rank, i % 11 ? 1u + unsigned(mix(i + 79) % 63) : 0u,
            mix(i + 0x147f83)});
    }
    return result;
}

struct Coverage {
    std::uint64_t consumed{}, distinct_sources{}, distinct_source_ranks{}, payload_lines{};
    std::size_t scratch_peak_bytes{};
};
Coverage coverage(const Population& population, std::span<const lc::Request> input,
                  std::size_t line_bytes) {
    require(line_bytes >= 8, "payload coverage line geometry");
    const auto capacity = population.layout.capacity();
    std::vector<std::uint8_t> visited(std::size_t(population.blocks.size()) * capacity);
    Coverage result;
    for (auto request : input) {
        const auto& rows = population.blocks[request.source].rows;
        auto rank = request.rank;
        for (unsigned step = 0; step < request.steps; ++step) {
            if (rank >= rows.size() || rows[rank].retired || rows[rank].length < 12) break;
            visited[std::size_t(request.source) * capacity + rank] = 1;
            ++result.consumed; rank = rows[rank].successor;
            if (rank >= rows.size()) break;
        }
    }
    std::vector<std::uintptr_t> line_ids;
    auto add_range = [&](std::uintptr_t address, unsigned length) {
        const auto first = address / line_bytes, last = (address + length - 1) / line_bytes;
        if (line_ids.empty() || line_ids.back() != first) line_ids.push_back(first);
        if (last != first) line_ids.push_back(last);
    };
    for (unsigned source = 0; source < population.blocks.size(); ++source) {
        const auto& block = population.blocks[source];
        const auto base = reinterpret_cast<std::uintptr_t>(block.windows.payload.data());
        unsigned offset = 0; bool source_visited = false;
        for (unsigned rank = 0; rank < block.rows.size(); ++rank) {
            const auto& row = block.rows[rank];
            if (visited[std::size_t(source) * capacity + rank]) {
                ++result.distinct_source_ranks; source_visited = true;
                // Starts come from the independent reservation model. Include
                // both ends of each demanded byte range, including split loads.
                add_range(base + offset, 8);
                add_range(base + offset + row.length - 4, 4);
            }
            offset += row.reserved;
        }
        result.distinct_sources += source_visited;
    }
    std::sort(line_ids.begin(), line_ids.end());
    result.payload_lines = std::unique(line_ids.begin(), line_ids.end()) - line_ids.begin();
    result.scratch_peak_bytes = visited.capacity() * sizeof(std::uint8_t) +
        line_ids.capacity() * sizeof(std::uintptr_t);
    // Both vectors are destroyed before timing; these are demanded line IDs,
    // not measured cache misses or a classification of the cache residence.
    return result;
}

struct Hook {
    std::span<const sp::const_view> views;
    std::uint64_t previous_completed{}, previous_quantum{}, calls{};
    unsigned requests{};
    static void visit(void* context, std::uint64_t quantum, std::uint64_t completed) noexcept {
        auto& hook = *static_cast<Hook*>(context);
        if (quantum == 1) { hook.previous_completed = 0; hook.previous_quantum = 0; }
        require(quantum > hook.previous_quantum, "returned-boundary quantum order within epoch");
        hook.previous_quantum = quantum;
        require(completed >= hook.previous_completed && completed <= hook.requests,
            "returned-boundary completion count");
        hook.previous_completed = completed; ++hook.calls;
        require(!hook.views.empty() && &hook.views.front().format() == &hook.views.back().format(),
            "shared layout and named views remain alive through boundary");
    }
};

std::uint64_t panels{}, checked_requests{}, checked_consumed{};
lc::Report check(Population& population, loom::Worker& worker,
    std::span<const lc::Request> input, lc::Options options, bool valid) {
    const auto expected = population.expected(input);
    const auto before = population.checksum();
    Hook hook{.views = population.views, .requests = unsigned(input.size())};
    auto report = lc::assess(worker, population.views, input, options, Hook::visit, &hook);
    require(report.outcomes == expected, "complete application outcomes differ from independent oracle");
    require(report.samples.size() == options.repetitions, "sample count");
    const auto consumed = std::accumulate(expected.begin(), expected.end(), std::uint64_t{},
        [](auto n, const auto& out) { return n + out.consumed; });
    for (const auto& sample : report.samples) {
        require(sample.completed == input.size() && sample.consumed == consumed, "completed/consumed logical counts");
        if (options.route == lc::Route::ring || options.route == lc::Route::manual) {
            if (valid) require(sample.stage_visits == 2 * consumed, "exact geometry+payload stage visits");
            const auto nonzero = static_cast<std::uint64_t>(std::count_if(input.begin(), input.end(),
                [](const auto& request) { return request.steps != 0; }));
            require(sample.accepted == nonzero, "one admission per nonzero request");
        }
    }
    require(before == population.checksum(), "read consumer mutated source planes");
    panels += options.repetitions; checked_requests += input.size() * options.repetitions;
    checked_consumed += consumed * options.repetitions;
    return report;
}
void same_counts(const lc::Sample& a, const lc::Sample& b) {
    require(a.accepted == b.accepted && a.refused == b.refused && a.completed == b.completed &&
        a.promoted == b.promoted && a.stage_visits == b.stage_visits && a.quanta == b.quanta &&
        a.refill_boundaries == b.refill_boundaries && a.consumed == b.consumed,
        "manual/Ring logical scheduling differs");
}

void qualification() {
    Runtime runtime(256ull << 20);
    unsigned schema_index = 0;
    // Cover both geometries/placements, narrow/wide maxima and O1/O2/O4.
    // The total extent also bounds each reservation: O1 narrows W2 to W1.
    // Variable W1 geometry cannot reach O4 at the fixed 256-slot capacity.
    for (auto family : {sp::geometry::absolute_ends, sp::geometry::sizes})
        for (auto placement : {sp::placement::grouped, sp::placement::separated})
            for (unsigned w : {1u, 2u})
                for (unsigned o : {1u, 2u, 4u}) {
                    if (o == 4 && w == 1) continue;
                    sp::layout::options d{family, placement,
                        std::uint16_t(w == 1 ? 255 : 1023), o == 1 ? 255u : o == 2 ? 24000u : 100000u,
                        {}, bool(schema_index & 1), bool(schema_index & 2)};
                    const std::array<unsigned, 4> assigned{1, 15, 16, 17};
                    Population population(runtime.worker, d, 4, assigned[schema_index % assigned.size()]);
                    const unsigned expected_width = o == 1 ? 1 : w;
                    require(population.layout.width_bytes() == expected_width && population.layout.offset_bytes() == o,
                        "reservation width bounded by maximum and total extent; requested offset class");
                    require(population.unaligned_tails != 0, "unaligned tail coverage");
                    population.release_allocations();
                    auto input = requests(population, 73);
                    check(population, runtime.worker, input, {lc::Route::direct}, true);
                    check(population, runtime.worker, input, {lc::Route::point_chain}, true);
                    lc::Options options{lc::Route::manual,
                        std::array<unsigned, 3>{1, 8, 32}[schema_index % 3],
                        (schema_index & 1) ? 4u : 0u,
                        std::array<unsigned, 3>{1, 17, 128}[(schema_index / 3) % 3]};
                    for (bool hints : {false, true}) {
                        options.hints = hints; options.route = lc::Route::manual;
                        auto manual = check(population, runtime.worker, input, options, true);
                        options.route = lc::Route::ring;
                        auto ring = check(population, runtime.worker, input, options, true);
                        same_counts(manual.samples.front(), ring.samples.front());
                    }
                    ++schema_index;
                }
    // Complete scheduler option matrix on ragged overlapping requests with more
    // work than the inventory. Width changes neither requests nor their work.
    sp::layout::options d{sp::geometry::sizes, sp::placement::separated, 255, 24000, {}, false, false};
    {
        Population population(runtime.worker, d, 5, 127);
        population.release_allocations(); auto input = requests(population, 93);
        for (unsigned width : {1u, 8u, 32u})
            for (unsigned quantum : {1u, 17u, 128u})
                for (unsigned overflow : {0u, 4u})
                    for (bool hints : {false, true}) {
                        lc::Options options{lc::Route::manual, width, overflow, quantum, 1, hints};
                        auto manual = check(population, runtime.worker, input, options, true);
                        options.route = lc::Route::ring;
                        auto ring = check(population, runtime.worker, input, options, true);
                        same_counts(manual.samples.front(), ring.samples.front());
                        const auto& sample = ring.samples.front();
                        require(sample.refused && sample.refill_boundaries > 1,
                            "ragged Ring did not exercise bounded refusal/retry/refill");
                        require(!overflow || sample.promoted, "overflow never promoted");
                    }
    }
    {
        Population population(runtime.worker, d, 2, 17);
        population.release_allocations();
        const std::array<lc::Request, 3> input{{{0, 9999, 0, 1}, {1, 3, 0, 2}, {0, 0, 0, 3}}};
        for (auto route : {lc::Route::direct, lc::Route::point_chain, lc::Route::manual, lc::Route::ring}) {
            const auto report = check(population, runtime.worker, input, {route, 1, 4, 1}, true);
            const auto& sample = report.samples.front();
            require(!sample.accepted && !sample.refused && !sample.promoted && !sample.stage_visits,
                "zero-step batch incurred native admission or work");
        }
    }
    {
        Population population(runtime.worker, d, 2, 0);
        population.release_allocations();
        const std::array<lc::Request, 2> input{{{0, 0, 1, 1}, {1, 9999, 0, 2}}};
        for (auto route : {lc::Route::direct, lc::Route::point_chain, lc::Route::manual, lc::Route::ring}) {
            for (bool hints : {false, true})
                check(population, runtime.worker, input, {route, 1, 4, 1, 1, hints}, false);
            const auto empty = check(population, runtime.worker, {}, {route, 1, 4, 1}, true);
            require(empty.outcomes.empty() && !empty.samples.front().accepted &&
                !empty.samples.front().stage_visits, "empty request batch incurred native work");
        }
    }
    // Full immortal fixed-width blocks omit all value metadata. Hinted native
    // admission must accept an empty geometry dependency and reach payload work.
    for (auto family : {sp::geometry::absolute_ends, sp::geometry::sizes}) {
        sp::layout::options fixed{family, sp::placement::grouped, 63, sp::slot_capacity * 12,
            std::uint16_t(12), true, true};
        Population population(runtime.worker, fixed, 2, 17);
        require(!population.layout.directory_bytes() && !population.layout.lengths_bytes(),
            "fixed/full/immortal fixture still has value metadata");
        population.release_allocations(); const auto input = requests(population, 73);
        for (auto route : {lc::Route::direct, lc::Route::point_chain, lc::Route::manual, lc::Route::ring})
            for (bool hints : {false, true})
                check(population, runtime.worker, input, {route, 8, 4, 17, 1, hints}, true);
    }
    {
        Population population(runtime.worker, d, 2, 17);
        population.release_allocations(); const auto input = requests(population, 43);
        lc::Options options{lc::Route::manual, 8, 4, 17, 3, true, true};
        const auto manual = check(population, runtime.worker, input, options, true);
        options.route = lc::Route::ring;
        const auto ring = check(population, runtime.worker, input, options, true);
        for (unsigned rep = 0; rep < options.repetitions; ++rep)
            same_counts(manual.samples[rep], ring.samples[rep]);
    }
    // Encoding remains valid; application validity deliberately fails at first
    // and later hops. Zero steps with an invalid rank still complete once.
    for (auto family : {sp::geometry::absolute_ends, sp::geometry::sizes}) {
        d.family = family;
        Population population(runtime.worker, d, 2, 17);
        population.retire(0, 0);
        population.change(0, 1, 11, 0);
        population.change(0, 2, 12, 24);
        population.change(0, 3, population.blocks[0].rows[3].length, 1);
        population.change(0, 4, population.blocks[0].rows[4].length, 0);
        population.release_allocations();
        std::vector<lc::Request> input{{0, 0, 4, 1}, {0, 1, 4, 2}, {0, 2, 1, 3},
            {0, 3, 4, 4}, {0, 4, 4, 5}, {0, 17, 1, 6}, {0, std::numeric_limits<unsigned>::max(), 0, 7},
            {1, 3, 23, 8}, {0, 2, 7, 9}, {0, 0, 0, 10}};
        for (auto route : {lc::Route::direct, lc::Route::point_chain, lc::Route::manual, lc::Route::ring})
            for (bool hints : {false, true}) {
                check(population, runtime.worker, input, {route, 1, 4, 1, 1, hints}, false);
                check(population, runtime.worker, input, {route, 8, 0, 17, 1, hints}, false);
            }
    }
    std::printf("check panels=%llu requests=%llu consumed=%llu schemas=%u; independent outcomes, "
        "unaligned tails, Allocation-release/Hold lifetime, irregular refill, errors and shutdown passed\n",
        static_cast<unsigned long long>(panels), static_cast<unsigned long long>(checked_requests),
        static_cast<unsigned long long>(checked_consumed), schema_index);
}
unsigned number(std::string_view text) {
    unsigned value{}; const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    require(result.ec == std::errc{} && result.ptr == text.data() + text.size(), "invalid unsigned CLI number");
    return value;
}
lc::Route route(std::string_view text) {
    if (text == "direct") return lc::Route::direct;
    if (text == "point-chain") return lc::Route::point_chain;
    if (text == "manual") return lc::Route::manual;
    if (text == "ring") return lc::Route::ring;
    fail("route must be direct, point-chain, manual or ring");
}
void bench(int argc, char** argv) {
    require(argc == 9 || argc == 10, "bench family blocks requests width reps route hints [placement]");
    const std::string_view family(argv[2]);
    require(family == "direct" || family == "width", "family must be direct or width");
    const auto block_count = number(argv[3]), request_count = number(argv[4]);
    const auto width = number(argv[5]), repetitions = number(argv[6]);
    const auto selected_route = route(argv[7]); const auto hints = number(argv[8]);
    const std::string_view placement = argc == 10 ? argv[9] : "grouped";
    require(placement == "grouped" || placement == "separated", "placement must be grouped or separated");
    require(block_count && block_count <= 65536 && request_count && request_count <= 1048576 &&
        (width == 1 || width == 8 || width == 32) && repetitions >= 3 && repetitions <= 100 && hints <= 1,
        "bounded benchmark dimensions; width1/8/32 and at least3 repetitions");
    Runtime runtime((std::uint64_t(block_count) * 20000 * 4) + (64ull << 20));
    sp::layout::options d{family == "direct" ? sp::geometry::absolute_ends : sp::geometry::sizes,
        placement == "grouped" ? sp::placement::grouped : sp::placement::separated,
        63, sp::slot_capacity * 63, {}, false, false};
    Population population(runtime.worker, d, block_count, 256);
    population.release_allocations();
    const auto request_setup_begin = Clock::now();
    const auto input = requests(population, request_count, true);
    const auto expected = population.expected(input);
    const auto hint_geometry = runtime.worker.hints.profile.geometry;
    const auto demand = coverage(population, input, hint_geometry.line_bytes);
    const auto before = population.checksum();
    population.release_oracle();
    const auto request_setup_ns = ns(Clock::now() - request_setup_begin);
    auto report = lc::assess(runtime.worker, population.views, input,
        {selected_route, width, 4, 128, repetitions, bool(hints), true});
    require(report.outcomes == expected && before == population.checksum(), "benchmark correctness/source immutability");
    const auto consumed = std::accumulate(expected.begin(), expected.end(), std::uint64_t{},
        [](auto n, const auto& out) { return n + out.consumed; });
    const auto statistics = oa::statistics(*runtime.worker.memory);
    std::printf("family=%s placement=%s blocks=%u assigned=256 requests=%u width=%u overflow=4 quantum=128 route=%s hints=%u\n",
        argv[2], placement.data(), block_count, request_count, width, argv[7], hints);
    std::printf("setup worker_ns=%llu population_ns=%llu request_oracle_checksum_ns=%llu consumer_ns=%llu close_consumer_ns=%llu "
        "frames_bytes=%zu scheduler_bytes=%zu request_bytes=%zu outcome_bytes=%zu source_bytes=%zu "
        "row_oracle_setup_bytes=%llu row_oracle_released=1 retained_expected_bytes=%zu\n",
        static_cast<unsigned long long>(runtime.setup_ns), static_cast<unsigned long long>(population.setup_ns),
        static_cast<unsigned long long>(request_setup_ns), static_cast<unsigned long long>(report.setup_ns),
        static_cast<unsigned long long>(report.close_ns),
        report.frame_bytes, report.scheduler_bytes, input.size() * sizeof(lc::Request),
        report.outcomes.size() * sizeof(lc::Outcome), population.views.size() * sizeof(sp::const_view),
        static_cast<unsigned long long>(population.oracle_bytes), expected.size() * sizeof(lc::Outcome));
    std::printf("footprint payload_capacity=%llu selected_value_bytes=%llu reservation_bytes=%llu directory=%llu "
        "lengths=%llu control=%llu allocation=%llu pool_mapped=%llu pool_reserved=%llu pool_metadata=%llu pool_idle=%llu\n",
        static_cast<unsigned long long>(population.payload_bytes), static_cast<unsigned long long>(population.selected_payload_bytes),
        static_cast<unsigned long long>(population.reserved_payload_bytes), static_cast<unsigned long long>(population.directory_bytes),
        static_cast<unsigned long long>(population.lengths_bytes), static_cast<unsigned long long>(population.control_bytes),
        static_cast<unsigned long long>(population.allocation_bytes), static_cast<unsigned long long>(statistics.mapped_bytes),
        static_cast<unsigned long long>(statistics.reserved_bytes), static_cast<unsigned long long>(statistics.metadata_bytes),
        static_cast<unsigned long long>(statistics.idle_bytes));
    require(demand.consumed == consumed, "independent trace coverage consumed count");
    std::printf("demand_coverage consumed=%llu distinct_sources=%llu distinct_source_ranks=%llu "
        "payload_lines=%llu payload_line_bytes=%llu line_bytes=%zu page_bytes=%zu "
        "scratch_peak_bytes=%zu scratch_released=1\n",
        static_cast<unsigned long long>(demand.consumed), static_cast<unsigned long long>(demand.distinct_sources),
        static_cast<unsigned long long>(demand.distinct_source_ranks), static_cast<unsigned long long>(demand.payload_lines),
        static_cast<unsigned long long>(demand.payload_lines * hint_geometry.line_bytes),
        hint_geometry.line_bytes, hint_geometry.page_bytes, demand.scratch_peak_bytes);
    std::printf("consumer_reads=first8_identity+tail4_link; value byte totals are logical, not bytes loaded; "
        "timings include admission/refill/drain and exclude printed setup/close\n");
    for (unsigned rep = 0; rep < report.samples.size(); ++rep) {
        const auto& sample = report.samples[rep];
        require(sample.completed == input.size() && sample.consumed == consumed, "benchmark work counts");
        std::printf("sample=%u elapsed_ns=%llu ns_per_request=%.6f ns_per_consumed=%.6f accepted=%llu refused=%llu "
            "completed=%llu promoted=%llu visits=%llu quanta=%llu refill_boundaries=%llu consumed=%llu\n", rep,
            static_cast<unsigned long long>(sample.elapsed_ns), double(sample.elapsed_ns) / request_count,
            (consumed ? double(sample.elapsed_ns) / consumed : 0.0), static_cast<unsigned long long>(sample.accepted),
            static_cast<unsigned long long>(sample.refused), static_cast<unsigned long long>(sample.completed),
            static_cast<unsigned long long>(sample.promoted), static_cast<unsigned long long>(sample.stage_visits),
            static_cast<unsigned long long>(sample.quanta), static_cast<unsigned long long>(sample.refill_boundaries),
            static_cast<unsigned long long>(sample.consumed));
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "check")) qualification();
    else if (std::string_view(argv[1]) == "bench") bench(argc, argv);
    else fail("usage: implementation_loom_check [check | bench family blocks requests width reps route hints [placement]]");
}
