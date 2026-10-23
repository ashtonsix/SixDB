#include "implementation_orbital.h"
#include <ikea/effects.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <sys/utsname.h>
#include <unistd.h>

namespace material = streampack_implementation::orbital_material;
namespace sp = ikea::streampack;
namespace oa = orbital::allocation;
using Clock = std::chrono::steady_clock;

namespace {
[[noreturn]] void fail(const char* why) {
    std::fprintf(stderr, "StreamPack Orbital benchmark: %s\n", why);
    std::abort();
}

void require(bool ok, const char* why) { if (!ok) fail(why); }

template<class Result> auto take(Result&& result, const char* why) {
    require(bool(result), why);
    return std::move(*result);
}

std::uint64_t ns(Clock::duration duration) {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) {
            char escaped[7]; std::snprintf(escaped, sizeof(escaped), "\\u%04x", unsigned(c));
            out += escaped;
        } else out += char(c);
    }
    return out + '"';
}

std::string_view environment(const char* key) {
    if (const char* value = std::getenv(key)) return value;
    return "unrecorded";
}

struct Runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    explicit Runtime(int cpu) {
        require(bool(loom::adopt(worker, cpu)), "adopt pinned Worker");
        arena = take(oa::open_arena(512ull << 20), "open Arena");
        require(bool(loom::configure(worker, arena, 0)), "configure direct Worker");
    }

    ~Runtime() {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (true) {
            const auto progress = loom::shutdown(worker);
            if (progress.kind() == orbital::Progress::Kind::done) break;
            require(progress.kind() != orbital::Progress::Kind::failed, "shutdown failed");
            require(Clock::now() < deadline, "storage users did not drain");
            loom::poll(worker, {256, 0});
            std::this_thread::yield();
        }
        require(arena->charged_bytes.load() == 0, "Arena still charged after close");
        loom::relinquish(worker);
    }
};

enum class Operation { replace, append, soft_pack, transfer };
const char* name(Operation op) {
    switch (op) {
    case Operation::replace: return "replace";
    case Operation::append: return "append_grow";
    case Operation::soft_pack: return "soft_pack";
    case Operation::transfer: return "hard_transfer";
    }
    std::abort();
}

struct Row {
    std::array<sp::byte, 48> data{};
    unsigned used{}, reserved{};
    bool live{true};
};

struct Plan {
    unsigned rank{};
    Row row;
};

struct Case {
    sp::geometry family;
    material::Placement placement;
    Operation operation;
    unsigned assigned;
    bool zero{};
    std::string id() const {
        return std::string(family == sp::geometry::sizes ? "width" : "direct") + "/" +
            (placement == material::Placement::grouped ? "grouped" : "separate") + "/" +
            name(operation) + "/" + std::to_string(assigned) + (zero ? "/zero" : "");
    }
};

struct Options {
    unsigned samples{512}, warmup{64}, retained{8}, repeats{1};
    int cpu{-1};
    bool raw{};
    std::string_view filter;
};

unsigned number(std::string_view value) {
    unsigned n{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), n);
    require(result.ec == std::errc{} && result.ptr == value.data() + value.size(), "invalid number");
    return n;
}

Options options(int argc, char** argv) {
    Options out;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--raw") { out.raw = true; continue; }
        require(i + 1 < argc, "option needs a value");
        const std::string_view value = argv[++i];
        if (arg == "--samples") out.samples = number(value);
        else if (arg == "--warmup") out.warmup = number(value);
        else if (arg == "--retain") out.retained = number(value);
        else if (arg == "--repeat") out.repeats = number(value);
        else if (arg == "--cpu") out.cpu = int(number(value));
        else if (arg == "--case") out.filter = value;
        else fail("unknown option");
    }
    require(out.samples && out.samples <= 65536 && out.warmup <= 4096 && out.retained <= 64 &&
            out.repeats && out.repeats <= 100, "unsupported dimensions");
    return out;
}

std::vector<Row> rows(const Case& c) {
    std::vector<Row> result(c.assigned);
    for (unsigned r = 0; r < c.assigned; ++r) {
        auto& row = result[r];
        row.used = c.zero ? 0 : 12 + (r * 13 % 19);
        row.reserved = c.zero ? 0 : row.used + 3 + r % 9;
        row.live = !((c.operation == Operation::soft_pack || c.operation == Operation::transfer) && r % 4 == 1);
        for (unsigned b = 0; b < row.used; ++b) row.data[b] = sp::byte((r * 71 + b * 19) & 255);
    }
    return result;
}

unsigned extent(std::span<const Row> rows) {
    unsigned sum = 0;
    for (const auto& row : rows) sum += row.reserved;
    return sum;
}

struct Reader {
    std::optional<material::Store::Scope> scope;
    material::Selection selected;
    unsigned object{};
    std::uint64_t footprint{};
};

struct Sample {
    std::uint64_t elapsed{}, cloned{}, issued{}, payload_issued{}, holder_issued{};
    std::uint64_t retained{}, maintenance{};
};

void verify(material::Selection selected, const std::vector<Row>& expected,
            std::span<const std::uint32_t> expected_holders) {
    auto source = material::read(selected);
    require(bool(source.validate()), "published content admission");
    require(source.size() == expected.size() && source.extent() == extent(expected), "count/extent oracle");
    for (unsigned r = 0; r < expected.size(); ++r) {
        const auto& row = expected[r];
        require(source.live_unchecked(r) == row.live, "liveness oracle");
        auto value = sp::value(source, r);
        if (!row.live) { require(!value && value.error() == sp::error::retired, "retired value oracle"); continue; }
        require(bool(value) && value->size() == row.used &&
            std::equal(value->begin(), value->end(), row.data.begin()), "application bytes oracle");
    }
    require(std::ranges::equal(material::holders(selected), expected_holders), "holder remap oracle");
}

std::uint64_t percentile(std::vector<std::uint64_t> values, unsigned percent) {
    std::sort(values.begin(), values.end());
    const auto rank = (values.size() * percent + 99) / 100;
    return values[rank ? rank - 1 : 0];
}

void run(const Case& c, const Options& o, unsigned repeat) {
    const auto setup_begin = Clock::now();
    Runtime runtime(o.cpu);
    auto& worker = runtime.worker;
    material::Store store(*std::pmr::new_delete_resource());
    const auto format = take(material::prepare_format({c.family, sp::placement::grouped,
        std::uint16_t(c.zero ? 0 : 48), unsigned(c.zero ? 0 : 256 * 48), {}, false, false}), "format");
    const auto original = rows(c);
    const auto original_extent = extent(original);
    std::vector<std::uint32_t> original_holders(format->layout.capacity(), material::absent);
    std::vector<std::uint16_t> ranks;
    for (unsigned r = 0; r < original.size(); ++r)
        if (original[r].live) { original_holders[r] = r; ranks.push_back(std::uint16_t(r)); }
    std::reverse(ranks.begin(), ranks.end());
    std::vector<std::uint32_t> remapped_holders = original_holders;
    std::fill(remapped_holders.begin(), remapped_holders.end(), material::absent);
    for (unsigned r = 0; r < ranks.size(); ++r) remapped_holders[ranks[r]] = r;
    std::vector<Row> transferred;
    for (auto r : ranks) { transferred.push_back(original[r]); transferred.back().reserved = original[r].used; }
    std::vector<Row> packed = original;
    for (auto& row : packed) row.reserved = row.live ? row.used : 0;
    const unsigned count = o.samples + o.warmup;
    std::vector<Plan> plans(count);
    for (unsigned i = 0; i < count; ++i) {
        auto& plan = plans[i];
        plan.rank = (i * 137u + 17u) % c.assigned;
        plan.row = c.operation == Operation::append ? Row{} : original[plan.rank];
        if (c.operation == Operation::append) {
            plan.row.used = c.zero ? 0 : 12 + i % 19;
            plan.row.reserved = c.zero ? 0 : plan.row.used + 5;
        }
        for (unsigned b = 0; b < plan.row.used; ++b) plan.row.data[b] = sp::byte((i * 31 + b * 11) & 255);
    }
    std::vector<material::Store::Change> changes;
    changes.reserve(count);
    std::array<ikea::journal::entry, 16> ignored_records; ikea::journal ignored{ignored_records};
    for (unsigned i = 0; i < count; ++i) {
        auto draft = take(material::Draft::create(worker, format, *std::pmr::new_delete_resource(),
            original_extent, c.placement), "create initial block");
        for (const auto& row : original) {
            ignored.used = 0;
            require(bool(sp::append(draft.write(), std::span(row.data).first(row.used), row.reserved, ignored)), "initial append");
        }
        for (unsigned r = 0; r < original.size(); ++r)
            if (!original[r].live) { ignored.used = 0; require(bool(sp::retire(draft.write(), r, ignored)), "initial retire"); }
        std::ranges::copy(original_holders, draft.holders().begin());
        auto owner = take(draft.capture(store), "initial capture");
        changes.push_back({{0, i + 1}, std::move(owner)});
    }
    {
        material::Store::Scope basis(store);
        material::Store::Prepared prepared(store);
        require(bool(store.prepare(basis, changes, prepared)), "initial prepare");
        require(bool(store.install(prepared)), "initial install");
    }
    changes.clear();
    store.drain();
    const auto setup_ns = ns(Clock::now() - setup_begin);
    const auto before = oa::statistics(*worker.memory);
    std::uint64_t mapped_peak = before.mapped_bytes, reserved_peak = before.reserved_bytes;
    std::uint64_t metadata_peak = before.metadata_bytes, charged_peak = runtime.arena->charged_bytes.load();
    const unsigned reader_count = std::max(1u, o.retained);
    auto readers = std::make_unique<Reader[]>(reader_count);
    std::uint64_t retained = 0, retained_peak = 0;
    std::vector<ikea::journal::entry> writes(std::max(2u + 256 * format->layout.append_effect_capacity(),
        format->layout.pack_effect_capacity(256)));
    std::vector<Sample> samples(o.samples);
    Clock::time_point measured_begin;
    for (unsigned i = 0; i < count; ++i) {
        Sample sample;
        auto& reader = readers[i % reader_count];
        const auto begin = Clock::now();
        if (i == o.warmup) measured_begin = begin;
        retained -= reader.footprint;
        reader.scope.reset(); reader.footprint = 0;
        reader.scope.emplace(store); reader.object = i;
        reader.selected = reader.scope->bind({0, i + 1});
        require(bool(reader.selected), "select old physical source");
        const auto old_control = reader.selected.control();
        reader.footprint = std::accumulate(old_control.extents.begin(), old_control.extents.end(), std::uint64_t{});
        retained += reader.footprint;
        {
            auto draft = c.operation == Operation::transfer ?
                take(material::Draft::create(worker, format, *std::pmr::new_delete_resource(),
                    extent(transferred), c.placement), "create transfer destination") :
                take(material::Draft::clone(worker, reader.selected, 0, c.placement), "clone retained source");
            sample.cloned = draft.copied_bytes;
            ikea::journal effects{writes};
            const auto& plan = plans[i];
            if (c.operation == Operation::replace)
                require(bool(sp::replace(draft.write(), plan.rank, std::span(plan.row.data).first(plan.row.used), effects)), "checked replace");
            else if (c.operation == Operation::append) {
                require(bool(draft.grow(original_extent + plan.row.reserved, format->layout.capacity())), "grow private block");
                require(bool(sp::append(draft.write(), std::span(plan.row.data).first(plan.row.used), plan.row.reserved, effects)), "checked append");
                draft.holders()[c.assigned] = c.assigned;
                sample.holder_issued = sizeof(std::uint32_t);
            } else if (c.operation == Operation::soft_pack)
                require(bool(sp::soft_pack(draft.write(), true, effects)), "checked soft pack");
            else {
                const auto source = material::read(reader.selected);
                require(bool(sp::transfer(source, ranks, draft.write(), true, effects)), "checked transfer");
                std::ranges::copy(remapped_holders, draft.holders().begin());
                sample.holder_issued = remapped_holders.size() * sizeof(std::uint32_t);
            }
            for (const auto& write : effects.entries()) {
                require(write.source == &draft.write() && write.plane < 4, "resolve named effects");
                const auto plane = draft.write().windows().planes()[write.plane];
                require(write.offset <= plane.size() && write.size <= plane.size() - write.offset,
                    "resolve admitted byte coordinates");
                sample.issued += write.size;
                if (write.plane == 0) sample.payload_issued += write.size;
            }
            auto owner = take(draft.capture(store), "capture new physical source");
            require(bool(material::publish(store, *reader.scope, {0, i + 1}, owner)), "prepare/install publication");
        }
        sample.retained = retained;
        if (!o.retained) { retained -= reader.footprint; reader.footprint = 0; reader.scope.reset(); }
        sample.maintenance = store.drain(128);
        loom::poll(worker, {32, 0});
        sample.elapsed = ns(Clock::now() - begin);
        retained_peak = std::max(retained_peak, sample.retained);
        const auto current = oa::statistics(*worker.memory);
        mapped_peak = std::max(mapped_peak, current.mapped_bytes);
        reserved_peak = std::max(reserved_peak, current.reserved_bytes);
        metadata_peak = std::max(metadata_peak, current.metadata_bytes);
        charged_peak = std::max(charged_peak, runtime.arena->charged_bytes.load());
        if (i >= o.warmup) samples[i - o.warmup] = sample;
    }
    const auto population_ns = ns(Clock::now() - measured_begin);
    const auto after = oa::statistics(*worker.memory);
    // Expected content comes from application plans, never from the encoding.
    for (unsigned i = 0; i < count; ++i) {
        auto expected = original;
        auto expected_holders = original_holders;
        if (c.operation == Operation::replace) expected[plans[i].rank] = plans[i].row;
        else if (c.operation == Operation::append) {
            expected.push_back(plans[i].row); expected_holders[c.assigned] = c.assigned;
        } else if (c.operation == Operation::soft_pack) expected = packed;
        else { expected = transferred; expected_holders = remapped_holders; }
        const auto selected = store.borrow().bind({0, i + 1});
        require(bool(selected), "current publication discovery");
        verify(selected, expected, expected_holders);
    }
    for (unsigned r = 0; r < reader_count; ++r)
        if (readers[r].scope) verify(readers[r].selected, original, original_holders);
    const auto close_begin = Clock::now();
    for (unsigned r = 0; r < reader_count; ++r) readers[r].scope.reset();
    store.drain(); loom::poll(worker, {256, 0});
    const auto close_ns = ns(Clock::now() - close_begin);
    std::vector<std::uint64_t> times; times.reserve(samples.size());
    Sample total;
    for (unsigned i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        times.push_back(sample.elapsed);
        total.elapsed += sample.elapsed; total.cloned += sample.cloned; total.issued += sample.issued;
        total.payload_issued += sample.payload_issued; total.holder_issued += sample.holder_issued;
        total.maintenance += sample.maintenance;
        if (o.raw) std::cout << "{\"type\":\"sample\",\"case\":" << quoted(c.id()) << ",\"repeat\":" << repeat
            << ",\"index\":" << i << ",\"ns\":" << sample.elapsed << ",\"clone_bytes\":" << sample.cloned
            << ",\"command_issued_bytes\":" << sample.issued << ",\"payload_issued_bytes\":" << sample.payload_issued
            << ",\"holder_issued_bytes\":" << sample.holder_issued << ",\"retained_footprint\":" << sample.retained
            << ",\"maintenance_steps\":" << sample.maintenance << "}\n";
    }
    std::cout << "{\"type\":\"summary\",\"case\":" << quoted(c.id()) << ",\"repeat\":" << repeat
        << ",\"samples\":" << o.samples << ",\"warmup\":" << o.warmup << ",\"retain_readers\":" << o.retained
        << ",\"p50_ns\":" << percentile(times, 50) << ",\"p99_ns\":" << percentile(times, 99)
        << ",\"mean_ns\":" << double(total.elapsed) / o.samples
        << ",\"operation_interval_ops_s\":" << double(o.samples) * 1e9 / double(total.elapsed)
        << ",\"population_wall_ops_s\":" << double(o.samples) * 1e9 / double(population_ns)
        << ",\"clone_bytes\":" << total.cloned << ",\"command_issued_bytes\":" << total.issued
        << ",\"payload_issued_bytes\":" << total.payload_issued << ",\"holder_issued_bytes\":" << total.holder_issued
        << ",\"retained_footprint_peak\":" << retained_peak << ",\"maintenance_steps\":" << total.maintenance
        << ",\"setup_ns\":" << setup_ns << ",\"residual_reader_close_ns\":" << close_ns
        << ",\"pool\":{\"mapped_peak\":" << mapped_peak << ",\"reserved_peak\":" << reserved_peak
        << ",\"metadata_peak\":" << metadata_peak << ",\"arena_charged_peak\":" << charged_peak
        << ",\"idle_after\":" << after.idle_bytes << ",\"allocations\":" << after.allocations - before.allocations
        << ",\"reuses\":" << after.reuses - before.reuses << ",\"growths\":" << after.growths - before.growths
        << ",\"relocations\":" << after.relocations - before.relocations
        << ",\"trim_pages\":" << after.trim_pages - before.trim_pages << "},\"oracle\":\"passed\"}\n";
}
} // namespace

int main(int argc, char** argv) {
    const auto o = options(argc, argv);
    std::cout.precision(12);
    std::vector<std::uint64_t> clock_samples;
    clock_samples.reserve(10000);
    for (unsigned i = 0; i < 10000; ++i) {
        const auto begin = Clock::now();
        clock_samples.push_back(ns(Clock::now() - begin));
    }
    utsname platform{};
    require(uname(&platform) == 0, "uname");
    std::cout << "{\"type\":\"context\",\"schema\":\"streampack-orbital-complete-v1\",\"source_id\":"
        << quoted(environment("SIXDB_SOURCE_ID")) << ",\"binary_sha256\":" << quoted(environment("SIXDB_BINARY_SHA256"))
        << ",\"compiler\":" << quoted(__clang_version__) << ",\"machine\":" << quoted(platform.machine)
        << ",\"kernel\":" << quoted(platform.release) << ",\"page_bytes\":" << sysconf(_SC_PAGESIZE)
        << ",\"requested_cpu\":" << o.cpu << ",\"clock_pair_p50_ns\":" << percentile(clock_samples, 50)
        << ",\"clock_pair_p99_ns\":" << percentile(clock_samples, 99)
        << ",\"notes\":\"Single-operation wall-clock quantiles include clock overhead without subtraction. Operation-interval throughput excludes statistics gaps; population wall throughput includes them. Prepared warmed distinct Objects; clone or fresh destination, checked command, effect resolution, capture, prepare/install, reader-window closing, drain(128), poll(32,0) included. Remaining reader close reported separately. Pool stats include warmup and exclude catalog PMR allocation; retained footprint is selected plane bytes, not unique mapped pages or all history protected by the cut. Command issued bytes exclude clone, holder stores and allocator zeroing. --raw emits every measured sample; no discarded losses.\"}\n";
    std::vector<Case> cases;
    for (auto family : {sp::geometry::absolute_ends, sp::geometry::sizes})
        for (auto placement : {material::Placement::grouped, material::Placement::separate})
            for (unsigned assigned : {3u, 15u, 64u}) {
                for (auto operation : {Operation::replace, Operation::append, Operation::soft_pack, Operation::transfer})
                    cases.push_back({family, placement, operation, assigned});
                cases.push_back({family, placement, Operation::append, assigned, true});
            }
    for (unsigned repeat = 0; repeat < o.repeats; ++repeat) {
        if (repeat) std::reverse(cases.begin(), cases.end());
        for (const auto& c : cases)
            if (c.id().find(o.filter) != std::string::npos) run(c, o, repeat);
    }
}
