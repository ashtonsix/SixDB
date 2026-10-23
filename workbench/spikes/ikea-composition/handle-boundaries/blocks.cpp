// Complete block/reuse witness for reusable-execution.md. Reuses the ordinary
// TuplePack bodies and the complete-consumer lessons in filter-composition/
// RUNTIME.md and MASK-GRADUATION.md: corrected distinct traces overturned the
// old catalogue-sharing win. This fixed permutation is not independent streams,
// a cold-memory simulation, owner publication, or a catalogue crossover claim.
#include <benchmark/benchmark.h>
#include <ikea/tuplepack.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace tp = ikea::tuplepack;
using word = std::uint64_t;
using Plan = tp::read_plan<64, 16>;
using Reader = tp::reader<64, const std::byte, 16>;
using Predicate = tp::predicate<64, const std::byte, 16>;
constexpr unsigned blocks = 64;
constexpr auto comparison = ikea::comparison::greater_equal;
enum class Route { retained, reprepare, oneshot, fresh };
enum class Trace { clustered, bursty, shuffled };
enum class Placement { tight, strided, mixed };
void require(bool ok) { if (!ok) { std::fputs("blocks oracle/fixture failure\n", stderr); std::abort(); } }
template <class Result> auto take(Result result) { require(bool(result)); return std::move(*result); }

struct Block {
    unsigned width = 0, rows = 0, stride = 0;
    std::vector<std::byte> data;
    std::optional<tp::const_view> view;
    std::optional<Reader> read;
};
struct Fixture {
    std::array<std::optional<tp::layout>, 8> laws;
    std::array<std::optional<Plan>, 8> plans;
    std::array<Block, blocks> block;
    std::array<unsigned, blocks> trace;
    std::vector<Predicate> retained;
    unsigned transitions = 0;
    explicit Fixture(Placement placement, Trace order) {
        for (unsigned w = 1; w <= 8; ++w) {
            laws[w - 1].emplace(take(tp::layout::make(
                1, std::array<tp::code, 1>{{{0, std::uint8_t(8 - w), std::uint8_t(w)}}})));
            plans[w - 1].emplace(take(Plan::make(*laws[w - 1], std::array<std::uint8_t, 1>{0})));
        }
        retained.reserve(blocks);
        for (unsigned id = 0; id < blocks; ++id) {
            auto& b = block[id]; b.width = id % 8 + 1;
            b.rows = id / 8 == 7 ? 13 : 16; // Exact tails, with no allocation padding grant.
            b.stride = 1 + (placement == Placement::strided ||
                           (placement == Placement::mixed && (id / 8) % 2));
            b.data.resize((b.rows - 1) * b.stride + 1);
            for (unsigned i = 0; i < b.data.size(); ++i)
                b.data[i] = std::byte((id * 71 + i * 43 + (i >> 2) * 17) & 255);
            b.view.emplace(take(tp::const_view::attach(*laws[b.width - 1], b.data, b.rows, b.stride)));
            b.read.emplace(take(tp::bind_reader(*plans[b.width - 1], *b.view)));
            retained.push_back(take(tp::bind_predicate(*b.read)));
            require(b.view->size() == b.rows && b.view->storage().size() == b.data.size());
        }
        unsigned n = 0;
        if (order == Trace::clustered) {
            for (unsigned w = 0; w < 8; ++w)
                for (unsigned b = 0; b < 8; ++b) trace[n++] = b * 8 + w;
        } else if (order == Trace::bursty) {
            for (unsigned pass = 0; pass < 4; ++pass)
                for (unsigned w = 0; w < 8; ++w)
                    for (unsigned b = 0; b < 2; ++b) trace[n++] = (pass * 2 + b) * 8 + w;
        } else {
            std::iota(trace.begin(), trace.end(), 0);
            std::uint32_t random = 0x6d2b79f5;
            for (unsigned i = blocks; i > 1; --i) {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                std::swap(trace[i - 1], trace[random % i]);
            }
        }
        std::array<unsigned, 8> widths{};
        unsigned tight = 0, tails = 0;
        for (const auto& b : block) { tight += b.stride == 1; tails += b.rows == 13; }
        require(tight == (placement == Placement::tight ? 64u : placement == Placement::strided ? 0u : 32u));
        require(tails == 8);
        std::array<bool, blocks> seen{};
        unsigned previous = 0;
        for (auto id : trace) {
            require(id < blocks && !seen[id]); seen[id] = true;
            const auto w = block[id].width; ++widths[w - 1];
            transitions += previous && previous != w; previous = w;
        }
        for (auto count : widths) require(count == blocks / 8);
        require(transitions == (order == Trace::clustered ? 7u : order == Trace::bursty ? 31u : 58u));
    }
    Fixture(const Fixture&) = delete;
    Fixture(Fixture&&) = delete; // Plans, named views and their addresses stay stable.
};
word activity(const Block& b, unsigned id, unsigned query) {
    const word full = (word{1} << b.rows) - 1;
    return (id / 8 + query) % 4 ? full : full & (0xa529u >> (query % 3));
}
word rhs(const Block& b, unsigned id, unsigned query, word epoch) {
    return (epoch + id * 29 + query * 17) & ((1u << b.width) - 1);
}
word oracle(const Block& b, word value, word active) {
    word answer = 0;
    for (unsigned row = 0; row < b.rows; ++row) {
        unsigned decoded = 0;
        const auto encoded = std::to_integer<unsigned>(b.data[row * b.stride]);
        for (unsigned bit = 0; bit < b.width; ++bit)
            decoded |= ((encoded >> (8 - b.width + bit)) & 1u) << bit;
        if ((active & (word{1} << row)) && decoded >= value) answer |= word{1} << row;
    }
    return answer;
}
template <class Operation>
word queries(const Operation& op, const Block& b, unsigned id, unsigned count, word epoch) {
    word sum = 0;
    for (unsigned q = 0; q < count; ++q)
        sum += take(op.template filter<comparison>(0, rhs(b, id, q, epoch), activity(b, id, q))) * (q + 1);
    return sum;
}
template <Route route> word traverse(const Fixture& f, unsigned count, word epoch) {
    word checksum = 0;
    for (auto id : f.trace) {
        const auto& b = f.block[id]; word sum;
        if constexpr (route == Route::retained) sum = queries(f.retained[id], b, id, count, epoch);
        else if constexpr (route == Route::reprepare) {
            const auto prepared = take(tp::bind_predicate(*b.read));
            sum = queries(prepared, b, id, count, epoch);
        } else if constexpr (route == Route::oneshot) sum = queries(*b.read, b, id, count, epoch);
        else {
            // Parent metadata chooses the shared law/plan; this command pays
            // placement admission and binding. Named locals outlive all queries.
            const auto view = take(tp::const_view::attach(*f.laws[b.width - 1], b.data, b.rows, b.stride));
            const auto read = take(tp::bind_reader(*f.plans[b.width - 1], view));
            const auto prepared = take(tp::bind_predicate(read));
            sum = queries(prepared, b, id, count, epoch);
        }
        checksum = std::rotl(checksum, 7) ^ (sum + id + 1); // Preserve visited order.
    }
    return checksum;
}
void verify(const Fixture& f) {
    for (auto id : f.trace) {
        const auto& b = f.block[id]; const word full = (word{1} << b.rows) - 1;
        const auto prepared = take(tp::bind_predicate(*b.read));
        const auto fresh_view = take(tp::const_view::attach(*f.laws[b.width - 1], b.data, b.rows, b.stride));
        const auto fresh_read = take(tp::bind_reader(*f.plans[b.width - 1], fresh_view));
        const auto fresh_prepared = take(tp::bind_predicate(fresh_read));
        // Exhaust the complete timed RHS domain and every timed activity
        // shape. This covers all iteration epochs, not just a warm-up query.
        for (word mask : {full, full & 0xa529u, full & (0xa529u >> 1), full & (0xa529u >> 2)})
            for (word value = 0; value < (word{1} << b.width); ++value) {
                const auto wanted = oracle(b, value, mask);
                require(take(f.retained[id].template filter<comparison>(0, value, mask)) == wanted);
                require(take(prepared.template filter<comparison>(0, value, mask)) == wanted);
                require(take(b.read->template filter<comparison>(0, value, mask)) == wanted);
                require(take(fresh_prepared.template filter<comparison>(0, value, mask)) == wanted);
            }
    }
    for (unsigned count : {1u, 8u, 64u}) for (word epoch : {word{0}, word{197}}) {
        word wanted = 0;
        for (auto id : f.trace) {
            const auto& b = f.block[id]; word sum = 0;
            for (unsigned q = 0; q < count; ++q)
                sum += oracle(b, rhs(b, id, q, epoch), activity(b, id, q)) * (q + 1);
            wanted = std::rotl(wanted, 7) ^ (sum + id + 1);
        }
        require(traverse<Route::retained>(f, count, epoch) == wanted);
        require(traverse<Route::reprepare>(f, count, epoch) == wanted);
        require(traverse<Route::oneshot>(f, count, epoch) == wanted);
        require(traverse<Route::fresh>(f, count, epoch) == wanted);
    }
}
template <Route route>
void run(benchmark::State& state, Placement placement, Trace order, unsigned count) {
    Fixture f(placement, order); verify(f); // All planning/fixtures/oracles precede timing.
    if constexpr (route != Route::retained) std::vector<Predicate>().swap(f.retained);
    word epoch = 0;
    for (auto _ : state) {
        // Vary in-domain queries across traversals; do not force per-value
        // store/reload barriers or let a constant whole traversal be hoisted.
        benchmark::DoNotOptimize(traverse<route>(f, count, epoch++));
    }
    using Counter = benchmark::Counter;
    const auto inverse_rate = Counter::Flags(Counter::kIsIterationInvariantRate | Counter::kInvert);
    state.counters["ns_per_block"] = Counter(blocks * 1e-9, inverse_rate);
    state.counters["ns_per_query"] = Counter(blocks * count * 1e-9, inverse_rate);
    state.counters["blocks"] = blocks; state.counters["queries_per_block"] = count;
    state.counters["width_transitions"] = f.transitions;
    state.counters["shared_plan_storage_bytes"] = sizeof(f.plans);
    state.counters["fixture_object_bytes"] = sizeof(f);
    state.counters["retained_predicate_bytes"] = f.retained.capacity() * sizeof(Predicate);
    std::size_t grants = 0, allocations = 0;
    for (const auto& b : f.block) { grants += b.data.size(); allocations += b.data.capacity(); }
    state.counters["payload_grant_bytes"] = grants;
    state.counters["payload_allocation_bytes"] = allocations;
    state.SetItemsProcessed(state.iterations() * blocks * count); // One item is one filter query.
}
template <Route route> void register_route(const char* route_name) {
    constexpr std::array placements{"tight", "strided", "mixed"};
    constexpr std::array traces{"clustered", "bursty", "shuffled"};
    for (unsigned p = 0; p < 3; ++p) for (unsigned t = 0; t < 3; ++t)
        for (unsigned count : {1u, 8u, 64u}) {
            const auto name = std::string("blocks/") + route_name + '/' + traces[t] + '/' +
                              placements[p] + "/q" + std::to_string(count);
            benchmark::RegisterBenchmark(name.c_str(), [p, t, count](benchmark::State& state) {
                run<route>(state, Placement(p), Trace(t), count);
            })->Unit(benchmark::kNanosecond);
        }
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--check-only") {
        for (unsigned p = 0; p < 3; ++p) for (unsigned t = 0; t < 3; ++t) verify(Fixture(Placement(p), Trace(t)));
        std::puts("blocks: all routes, RHS domains, masks, exact grants and ordered traces passed");
        return 0;
    }
    register_route<Route::retained>("retained"); register_route<Route::reprepare>("reprepare");
    register_route<Route::oneshot>("oneshot");
    register_route<Route::fresh>("fresh");
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
}
