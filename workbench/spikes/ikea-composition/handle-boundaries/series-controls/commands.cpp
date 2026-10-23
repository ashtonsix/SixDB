#include "oracle.h"
#include <benchmark/benchmark.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <string_view>

namespace series_controls {
namespace {
constexpr unsigned block_count = 64;
enum class route { columns, shared_fresh, shared_retained, copied_fresh, copied_retained, typed };
enum class init_route { columns, dispatch, typed };
enum class trace { clustered, permuted };
void require(bool ok) { if (!ok) { std::fputs("series-controls command oracle failed\n", stderr); std::abort(); } }
template <class T> auto take(T value) { require(bool(value)); return std::move(*value); }
struct block {
    shape control{0};
    std::vector<word> a, b;
    std::vector<std::byte> payload;
    destination write{{0}, {}, 0};
    std::uint8_t first_a = 0, first_b = 0;
};
struct fixture {
    std::array<block, block_count> blocks;
    std::array<unsigned, block_count> order;
    std::vector<shared_reader<word>> shared;
    std::vector<sp::erased_reader<word>> copied;
    unsigned transitions = 0;
    fixture(sp::geometry geometry, trace pattern, bool retain_shared, bool retain_copied) {
        constexpr std::array local{1u, 5u, 7u, 12u, 20u, 28u, 31u, 63u};
        constexpr std::array striped{1u, 2u, 3u, 4u, 5u, 7u, 12u, 20u};
        constexpr std::array counts{0u, 13u, 16u, 31u, 256u, 13u, 31u, 256u};
        if (retain_shared) shared.reserve(block_count);
        if (retain_copied) copied.reserve(block_count);
        for (unsigned id = 0; id < block_count; ++id) {
            auto& b = blocks[id]; const auto width = (geometry == sp::geometry::local ? local : striped)[id % 8];
            b.control = take(shape::make({width, 0, geometry}));
            b.a = values(width, counts[id / 8], 0x248761ab + id);
            b.b = values(width, counts[id / 8], 0x8715fe29 + id);
            b.payload = encode_reference(b.control, b.a);
            const auto other = encode_reference(b.control, b.b);
            if (!b.payload.empty()) {
                b.first_a = std::to_integer<unsigned>(b.payload.front());
                b.first_b = std::to_integer<unsigned>(other.front());
            }
            b.write = {b.control, b.payload, b.a.size()};
            if (retain_shared) shared.push_back(take(bind_reader<word>(b.control, b.payload, b.a.size())));
            if (retain_copied) copied.push_back(take(sp::bind_reader<word>(
                sp::dense_input{width, geometry, b.payload, b.a.size()})));
        }
        unsigned n = 0;
        for (unsigned width = 0; width < 8; ++width)
            for (unsigned group = 0; group < 8; ++group) order[n++] = group * 8 + width;
        if (pattern == trace::permuted) {
            std::uint32_t random = 0x6d2b79f5;
            for (unsigned i = block_count; i > 1; --i) {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                std::swap(order[i - 1], order[random % i]);
            }
        }
        std::array<bool, block_count> seen{};
        unsigned previous = 0;
        for (auto id : order) {
            require(id < block_count && !seen[id]); seen[id] = true;
            auto width = blocks[id].control.width(); transitions += previous && previous != width; previous = width;
        }
        if (pattern == trace::clustered) require(transitions == 7);
    }
    fixture(const fixture&) = delete;
    fixture(fixture&&) = delete;
};

template <operation O, class Reader> word queries(const Reader& read, const command& c, unsigned reuse, word epoch) {
    word sum = 0; std::array<word, 32> out;
    for (unsigned q = 0; q < reuse; ++q) {
        if constexpr (O == operation::point) sum += read.get_unchecked(point_row(c, q, epoch));
        else if constexpr (O == operation::range) {
            auto [first, count] = range_rows(c, q, epoch); read.read_unchecked(first, count, out.data());
            sum += consume(std::span(out).first(count));
        } else {
            read.read16_unchecked(region_row(c, q, epoch), out.data()); sum += consume(std::span(out).first(16));
        }
    }
    return sum;
}
template <operation O> word column_queries(shape control, const command& c, unsigned reuse, word epoch) {
    word sum = 0; std::array<word, 32> out;
    const auto& table = read_columns<word>;
    if constexpr (O == operation::point) {
        const auto selected = table.points[control.index];
        for (unsigned q = 0; q < reuse; ++q) sum += selected(c.data, point_row(c, q, epoch));
    } else if constexpr (O == operation::range) {
        const auto selected = table.ranges[control.index];
        for (unsigned q = 0; q < reuse; ++q) {
            auto [first, count] = range_rows(c, q, epoch); selected(c.data, first, count, out.data());
            sum += consume(std::span(out).first(count));
        }
    } else {
        const auto selected = table.regions[control.index];
        for (unsigned q = 0; q < reuse; ++q) {
            selected(c.data, region_row(c, q, epoch), out.data()); sum += consume(std::span(out).first(16));
        }
    }
    return sum;
}
template <route R, operation O>
[[gnu::noinline]] word traverse(const fixture& f, unsigned reuse, word epoch) {
    word checksum = 0;
    for (auto id : f.order) {
        const auto& b = f.blocks[id]; const command c{b.payload.data(), b.a.size(), id};
        word sum = 0;
        // No eager table lookup, bind or payload read for empty work. Native16
        // requires sixteen logical rows, even when the occupied tile is larger.
        if (c.rows && (O != operation::read16 || c.rows >= 16)) {
            if constexpr (R == route::columns) sum = column_queries<O>(b.control, c, reuse, epoch);
            else if constexpr (R == route::shared_retained) sum = queries<O>(f.shared[id], c, reuse, epoch);
            else if constexpr (R == route::copied_retained) sum = queries<O>(f.copied[id], c, reuse, epoch);
            else if constexpr (R == route::typed) sum = typed_columns[b.control.index](O, c, reuse, epoch);
            else if constexpr (R == route::shared_fresh) {
                const auto read = take(bind_reader<word>(b.control, b.payload, c.rows));
                sum = queries<O>(read, c, reuse, epoch);
            } else {
                const auto read = take(sp::bind_reader<word>(sp::dense_input{
                    b.control.width(), b.control.geometry(), b.payload, c.rows}));
                sum = queries<O>(read, c, reuse, epoch);
            }
        }
        checksum = std::rotl(checksum, 7) ^ (sum + id + 1);
    }
    return checksum;
}
template <init_route R>
[[gnu::noinline]] word traverse_initialize(const fixture& f, unsigned reuse, word epoch) {
    word checksum = 0; std::array<ikea::journal::entry, 68> records;
    ikea::journal effects{records};
    for (auto id : f.order) {
        const auto& b = f.blocks[id]; word sum = 0;
        if (!b.a.empty()) {
            if constexpr (R == init_route::typed)
                sum = typed_initialize_columns[b.control.index](b.write, b.a, b.b, reuse, epoch, effects);
            else {
                const auto selected = initialize_columns[b.control.index];
                for (unsigned q = 0; q < reuse; ++q) {
                    effects.used = 0; const auto& input = (epoch + q) & 1 ? b.a : b.b;
                    if constexpr (R == init_route::columns) require(bool(selected(b.write, input, effects)));
                    else require(bool(initialize_dispatch(b.write, input, effects)));
                    sum += initialized(b.write, effects);
                }
            }
        }
        checksum = std::rotl(checksum, 7) ^ (sum + id + 1);
    }
    return checksum;
}
word wanted(const fixture& f, operation op, unsigned reuse, word epoch) {
    word checksum = 0;
    for (auto id : f.order) {
        const auto& b = f.blocks[id]; const command c{b.payload.data(), b.a.size(), id}; word sum = 0;
        if (c.rows && (op != operation::read16 || c.rows >= 16)) for (unsigned q = 0; q < reuse; ++q) {
            if (op == operation::point) sum += b.a[point_row(c, q, epoch)];
            else if (op == operation::range) { auto [first, n] = range_rows(c, q, epoch); sum += consume(std::span(b.a).subspan(first, n)); }
            else sum += consume(std::span(b.a).subspan(region_row(c, q, epoch), 16));
        }
        checksum = std::rotl(checksum, 7) ^ (sum + id + 1);
    }
    return checksum;
}
word wanted_initialize(const fixture& f, unsigned reuse, word epoch) {
    word checksum = 0;
    for (auto id : f.order) {
        const auto& b = f.blocks[id]; word sum = 0;
        if (!b.a.empty()) for (unsigned q = 0; q < reuse; ++q)
            sum += 1 + b.payload.size() + (((epoch + q) & 1) ? b.first_a : b.first_b);
        checksum = std::rotl(checksum, 7) ^ (sum + id + 1);
    }
    return checksum;
}
void counters(benchmark::State& state, const fixture& f, unsigned reuse, operation op, bool initializing) {
    std::size_t active = 0, rows = 0, payload = 0, inputs = 0;
    for (unsigned id = 0; id < block_count; ++id) {
        const auto& b = f.blocks[id]; const auto n = b.a.size(); payload += b.payload.size();
        inputs += (b.a.capacity() + b.b.capacity()) * sizeof(word);
        if (!n || (!initializing && op == operation::read16 && n < 16)) continue;
        ++active;
        rows += initializing ? n : op == operation::point ? 1 : op == operation::read16 ? 16 : std::min<std::size_t>(id & 1 ? 31 : 13, n);
    }
    state.counters["visited_blocks"] = block_count; state.counters["active_blocks"] = active;
    state.counters["queries_per_active_block"] = reuse; state.counters["issued_queries"] = active * reuse;
    state.counters["logical_rows"] = rows * reuse; state.counters["width_transitions"] = f.transitions;
    state.counters["occupied_payload_bytes"] = payload; state.counters["resident_input_bytes"] = inputs;
    state.counters["fixture_object_bytes"] = sizeof(fixture);
    state.counters["retained_binding_bytes"] = f.shared.capacity() * sizeof(shared_reader<word>) + f.copied.capacity() * sizeof(sp::erased_reader<word>);
    state.counters["operation_table_bytes"] = sizeof(read_columns<std::uint8_t>) + sizeof(read_columns<std::uint16_t>) + sizeof(read_columns<std::uint32_t>) + sizeof(read_columns<word>) + sizeof(initialize_columns) + sizeof(typed_columns) + sizeof(typed_initialize_columns) + sizeof(extent_columns);
    state.counters["journal_capacity_entries"] = initializing ? 68 : 0;
    state.SetItemsProcessed(state.iterations() * active * reuse);
}
template <route R, operation O> void run(benchmark::State& state, sp::geometry geometry, trace pattern, unsigned reuse) {
    fixture f(geometry, pattern, R == route::shared_retained, R == route::copied_retained);
    for (word epoch : {0u, 197u}) require(traverse<R, O>(f, reuse, epoch) == wanted(f, O, reuse, epoch));
    word epoch = 0;
    for (auto _ : state) benchmark::DoNotOptimize(traverse<R, O>(f, reuse, epoch++));
    counters(state, f, reuse, O, false);
}
template <init_route R> void run_initialize(benchmark::State& state, sp::geometry geometry, trace pattern, unsigned reuse) {
    fixture f(geometry, pattern, false, false);
    for (word epoch : {0u, 197u}) {
        require(traverse_initialize<R>(f, reuse, epoch) == wanted_initialize(f, reuse, epoch));
        for (const auto& b : f.blocks) {
            const auto& input = ((epoch + reuse - 1) & 1) ? b.a : b.b;
            require(b.payload == encode_reference(b.control, input));
        }
    }
    for (auto& b : f.blocks) { const auto encoded = encode_reference(b.control, b.a); std::copy(encoded.begin(), encoded.end(), b.payload.begin()); }
    word epoch = 0;
    for (auto _ : state) benchmark::DoNotOptimize(traverse_initialize<R>(f, reuse, epoch++));
    counters(state, f, reuse, operation::point, true);
}
template <class Add> void cases(std::string prefix, Add add) {
    constexpr std::array geometries{"local", "striped"}; constexpr std::array patterns{"clustered", "permuted"};
    for (unsigned g = 0; g < 2; ++g) for (unsigned t = 0; t < 2; ++t) for (unsigned q : {1u, 8u, 64u}) {
        const auto name = prefix + '/' + patterns[t] + '/' + geometries[g] + "/q" + std::to_string(q);
        benchmark::RegisterBenchmark(name.c_str(), [g, t, q, add](benchmark::State& s) { add(s, sp::geometry(g), trace(t), q); })->Unit(benchmark::kNanosecond);
    }
}
template <operation O> void register_read(std::string name) {
    const auto prefix = "series/read/" + name + '/';
    cases(prefix + "columns", run<route::columns, O>);
    cases(prefix + "columns-duplicate", run<route::columns, O>); // Exact same complete command function.
    cases(prefix + "shared-fresh", run<route::shared_fresh, O>);
    cases(prefix + "shared-retained", run<route::shared_retained, O>);
    cases(prefix + "copied-fresh", run<route::copied_fresh, O>);
    cases(prefix + "copied-retained", run<route::copied_retained, O>);
    cases(prefix + "typed", run<route::typed, O>);
}
void check_commands() {
    for (auto geometry : {sp::geometry::local, sp::geometry::striped}) for (auto pattern : {trace::clustered, trace::permuted}) for (unsigned q : {1u, 8u, 64u}) {
        fixture f(geometry, pattern, true, true);
        for (word epoch : {0u, 197u}) {
#define READ_CHECK(O, R) require(traverse<route::R, operation::O>(f, q, epoch) == wanted(f, operation::O, q, epoch))
#define ROUTE_CHECK(R) READ_CHECK(point, R); READ_CHECK(range, R); READ_CHECK(read16, R)
            ROUTE_CHECK(columns); ROUTE_CHECK(shared_fresh); ROUTE_CHECK(shared_retained);
            ROUTE_CHECK(copied_fresh); ROUTE_CHECK(copied_retained); ROUTE_CHECK(typed);
#undef ROUTE_CHECK
#undef READ_CHECK
            require(traverse_initialize<init_route::columns>(f, q, epoch) == wanted_initialize(f, q, epoch));
            require(traverse_initialize<init_route::dispatch>(f, q, epoch) == wanted_initialize(f, q, epoch));
            require(traverse_initialize<init_route::typed>(f, q, epoch) == wanted_initialize(f, q, epoch));
            for (auto& b : f.blocks) { const auto encoded = encode_reference(b.control, b.a); std::copy(encoded.begin(), encoded.end(), b.payload.begin()); }
        }
    }
    std::puts("series-controls: all300 named complete-command routes, mixed-width permutations/reuse, exact row work and independent initialization checks passed");
}
}
void facts() {
    std::printf("{\"shape_bytes\":%zu,\"shape_align\":%zu,\"shared_reader_bytes\":%zu,\"shared_reader_align\":%zu,\"copied_reader_bytes\":%zu,\"typed_dense_reader_bytes\":%zu,\"shared_reader_trivial_copy\":%s,\"read_table_bytes\":%zu,\"initialize_table_bytes\":%zu,\"typed_table_bytes\":%zu,\"typed_initialize_table_bytes\":%zu,\"extent_table_bytes\":%zu,\"fixture_object_bytes\":%zu,\"block_object_bytes\":%zu,\"cases\":300}\n",
        sizeof(shape), alignof(shape), sizeof(shared_reader<word>), alignof(shared_reader<word>), sizeof(sp::erased_reader<word>),
        sizeof(sp::reader<sp::dense_source<sp::layout<31>>>), std::is_trivially_copyable_v<shared_reader<word>> ? "true" : "false",
        sizeof(read_columns<std::uint8_t>) + sizeof(read_columns<std::uint16_t>) + sizeof(read_columns<std::uint32_t>) + sizeof(read_columns<word>),
        sizeof(initialize_columns), sizeof(typed_columns), sizeof(typed_initialize_columns), sizeof(extent_columns), sizeof(fixture), sizeof(block));
}
}
int main(int argc, char** argv) {
    using namespace series_controls;
    if (argc == 2 && std::string_view(argv[1]) == "--check-only") { check(); check_commands(); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--facts") { facts(); return 0; }
    register_read<operation::point>("point"); register_read<operation::range>("range"); register_read<operation::read16>("read16");
    cases("series/initialize/columns", run_initialize<init_route::columns>);
    cases("series/initialize/columns-duplicate", run_initialize<init_route::columns>);
    cases("series/initialize/dispatch", run_initialize<init_route::dispatch>);
    cases("series/initialize/typed", run_initialize<init_route::typed>);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
}
