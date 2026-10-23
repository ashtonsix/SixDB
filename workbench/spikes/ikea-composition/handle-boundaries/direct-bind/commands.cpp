// Complete preparation commands from an already validated layout. Reuses the
// block/reuse study's 64-block population, three width orders and exact tails.
// Plan construction here means computation, not cold instruction/data caches.
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
constexpr unsigned blocks = 64;
constexpr auto comparison = ikea::comparison::greater_equal;
enum class Route { construct_plan, shared_plan, direct };
enum class Trace { clustered, bursty, shuffled };
enum class Placement { tight, strided, mixed };
void need(bool ok) {
    if (!ok) { std::fputs("direct predicate command/oracle failure\n", stderr); std::abort(); }
}
template <class Result> auto take(Result result) { need(bool(result)); return std::move(*result); }
struct Block {
    unsigned width = 0, rows = 0, stride = 0;
    std::vector<std::byte> bytes;
};
struct Fixture {
    std::array<std::optional<tp::layout>, 8> laws;
    std::array<std::optional<Plan>, 8> plans;
    std::array<Block, blocks> block;
    std::array<unsigned, blocks> trace;
    unsigned transitions = 0;
    Fixture(Placement placement, Trace order) {
        for (unsigned w = 1; w <= 8; ++w) {
            laws[w - 1].emplace(take(tp::layout::make(
                1, std::array<tp::code, 1>{{{0, std::uint8_t(8 - w), std::uint8_t(w)}}})));
            plans[w - 1].emplace(take(Plan::make(*laws[w - 1], std::array<std::uint8_t, 1>{0})));
        }
        for (unsigned id = 0; id < blocks; ++id) {
            auto& b = block[id]; b.width = id % 8 + 1;
            b.rows = id / 8 == 7 ? 13 : 16;
            b.stride = 1 + (placement == Placement::strided ||
                           (placement == Placement::mixed && (id / 8) % 2));
            b.bytes.resize((b.rows - 1) * b.stride + 1);
            for (unsigned i = 0; i < b.bytes.size(); ++i)
                b.bytes[i] = std::byte((id * 71 + i * 43 + (i >> 2) * 17) & 255);
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
        std::array<bool, blocks> seen{};
        unsigned previous = 0;
        for (auto id : trace) {
            need(id < blocks && !seen[id]); seen[id] = true;
            const auto width = block[id].width;
            transitions += previous && previous != width; previous = width;
        }
        need(transitions == (order == Trace::clustered ? 7u : order == Trace::bursty ? 31u : 58u));
    }
};
word activity(const Block& b, unsigned id, unsigned q) {
    const word full = (word{1} << b.rows) - 1;
    return (id / 8 + q) % 4 ? full : full & (0xa529u >> (q % 3));
}
word rhs(const Block& b, unsigned id, unsigned q, word epoch) {
    return (epoch + id * 29 + q * 17) & ((1u << b.width) - 1);
}
word oracle(const Block& b, word query, word active) {
    word result = 0;
    for (unsigned row = 0; row < b.rows; ++row) {
        const auto encoded = std::to_integer<unsigned>(b.bytes[row * b.stride]);
        unsigned value = 0;
        for (unsigned bit = 0; bit < b.width; ++bit)
            value |= ((encoded >> (8 - b.width + bit)) & 1u) << bit;
        if ((active & (word{1} << row)) && value >= query) result |= word{1} << row;
    }
    return result;
}
template <class Operation> word queries(const Operation& op, const Block& b, unsigned id,
                                       unsigned count, word epoch) {
    word sum = 0;
    for (unsigned q = 0; q < count; ++q)
        sum += take(op.template filter<comparison>(0, rhs(b, id, q, epoch), activity(b, id, q))) * (q + 1);
    return sum;
}
template <Route route> [[gnu::noinline]] word traverse(const Fixture& f, unsigned count, word epoch) {
    word checksum = 0;
    for (auto id : f.trace) {
        const auto& b = f.block[id]; const auto& law = *f.laws[b.width - 1];
        // Every route admits the same exact placement, once per block command.
        const auto view = take(tp::const_view::attach(law, b.bytes, b.rows, b.stride));
        word sum;
        if constexpr (route == Route::construct_plan) {
            const auto plan = take(Plan::make(law, std::array<std::uint8_t, 1>{0}));
            const auto op = take(tp::bind_predicate(plan, view));
            sum = queries(op, b, id, count, epoch);
        } else if constexpr (route == Route::shared_plan) {
            const auto op = take(tp::bind_predicate(*f.plans[b.width - 1], view));
            sum = queries(op, b, id, count, epoch);
        } else {
            const auto op = take(tp::bind_predicate<16>(law, 0, view));
            sum = queries(op, b, id, count, epoch);
        }
        checksum = std::rotl(checksum, 7) ^ (sum + id + 1);
    }
    return checksum;
}
void verify(const Fixture& f) {
    for (auto id : f.trace) {
        const auto& b = f.block[id]; const auto& law = *f.laws[b.width - 1];
        const auto view = take(tp::const_view::attach(law, b.bytes, b.rows, b.stride));
        const auto direct = take(tp::bind_predicate<16>(law, 0, view));
        const auto planned = take(tp::bind_predicate(*f.plans[b.width - 1], view));
        const word full = (word{1} << b.rows) - 1;
        for (word mask : {full, full & 0xa529u, full & (0xa529u >> 1), full & (0xa529u >> 2)})
            for (word query = 0; query < (word{1} << b.width); ++query) {
                const auto expected = oracle(b, query, mask);
                need(take(direct.template filter<comparison>(0, query, mask)) == expected);
                need(take(planned.template filter<comparison>(0, query, mask)) == expected);
            }
    }
    for (unsigned count : {1u, 8u, 64u}) for (word epoch : {word{0}, word{197}}) {
        word expected = 0;
        for (auto id : f.trace) {
            const auto& b = f.block[id]; word sum = 0;
            for (unsigned q = 0; q < count; ++q)
                sum += oracle(b, rhs(b, id, q, epoch), activity(b, id, q)) * (q + 1);
            expected = std::rotl(expected, 7) ^ (sum + id + 1);
        }
        need(traverse<Route::construct_plan>(f, count, epoch) == expected);
        need(traverse<Route::shared_plan>(f, count, epoch) == expected);
        need(traverse<Route::direct>(f, count, epoch) == expected);
    }
}
template <Route route> void run(benchmark::State& state, Placement placement, Trace order, unsigned count) {
    const Fixture f(placement, order);
    word epoch = 0;
    for (auto _ : state) benchmark::DoNotOptimize(traverse<route>(f, count, epoch++));
    using Counter = benchmark::Counter;
    const auto rate = Counter::Flags(Counter::kIsIterationInvariantRate | Counter::kInvert);
    state.counters["ns_per_command"] = Counter(blocks * 1e-9, rate);
    state.counters["ns_per_query"] = Counter(blocks * count * 1e-9, rate);
    state.counters["blocks"] = blocks; state.counters["queries_per_command"] = count;
    state.counters["width_transitions"] = f.transitions;
    state.counters["predicate_bytes"] = sizeof(tp::predicate<const std::byte, 16>);
    state.counters["shared_plan_bank_bytes"] = route == Route::shared_plan ? sizeof(f.plans) : 0;
    state.counters["transient_plan_bytes"] = route == Route::construct_plan ? sizeof(Plan) : 0;
    state.counters["fixture_plan_bank_bytes"] = sizeof(f.plans);
    state.counters["validated_layout_bank_bytes"] = sizeof(f.laws);
    state.SetItemsProcessed(state.iterations() * blocks * count);
}
template <Route route> void register_route(const char* name) {
    constexpr std::array placements{"tight", "strided", "mixed"};
    constexpr std::array traces{"clustered", "bursty", "shuffled"};
    for (unsigned p = 0; p < 3; ++p) for (unsigned t = 0; t < 3; ++t)
        for (unsigned q : {1u, 8u, 64u}) {
            const auto label = std::string("prepare/") + name + '/' + traces[t] + '/' +
                               placements[p] + "/q" + std::to_string(q);
            benchmark::RegisterBenchmark(label.c_str(), [p, t, q](benchmark::State& state) {
                run<route>(state, Placement(p), Trace(t), q);
            })->Unit(benchmark::kNanosecond);
        }
}
} // namespace
int main(int argc, char** argv) {
    // Preflight is independent and outside every timed benchmark command.
    for (unsigned p = 0; p < 3; ++p) for (unsigned t = 0; t < 3; ++t)
        verify(Fixture(Placement(p), Trace(t)));
    if (argc == 2 && std::string_view(argv[1]) == "--check-only") {
        std::puts("direct predicate: all RHS domains, masks, exact tails, ordered complete commands passed");
        return 0;
    }
    register_route<Route::construct_plan>("construct_plan");
    register_route<Route::shared_plan>("shared_plan"); register_route<Route::direct>("direct");
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
}
