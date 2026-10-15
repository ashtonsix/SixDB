#pragma once
#include <ikea/seriespack.h>
#include <ikea/seriespack/author/chain.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>

namespace runtime {
namespace sp = ikea::seriespack;
using C = ikea::comparison;
using word = std::uint64_t;
constexpr unsigned windows = 64;
template <unsigned K> using Source = sp::dense_source<sp::format<K>>;
template <unsigned B> using Packet = sp::native::values<B * 8>;
template <unsigned B> using Chain = sp::chain<B * 8, 4>;
struct State {
    const std::uint8_t *bytes{};
    const void *plan{};
    word query{};
    std::uint16_t result{};
};
using Call = std::uint16_t (*)(State &, std::size_t, word, std::uint16_t);
struct Fixture {
    unsigned width;
    std::vector<std::uint8_t> bytes;
    std::vector<word> truth;
    std::array<word, windows> queries;
    explicit Fixture(unsigned k);
};
struct Entry {
    Call fused, shared, one, selected;
    State state;
    const void *one_plan;
    const word *queries;
    unsigned width, op;
};
extern std::vector<Entry> catalogue;
extern unsigned iterations, repeats;
extern word observed;
extern bool check_only;
bool oracle(unsigned op, word a, word b);
void verify(Call call, State &state, const Fixture &, unsigned op);
void measure_bound(Call, State, const Fixture &, unsigned op, const char *name,
                   std::uint16_t active);
void catalogue_bench();

[[gnu::always_inline]] inline unsigned next(word mask, unsigned salt) {
    return unsigned((mask * 0x9e3779b97f4a7c15ULL) >> 58) ^ (salt & 63);
}
template <class CallBody>
[[gnu::noinline]] void measure(const Fixture &fixture, unsigned op, const char *name,
                               std::uint16_t active, CallBody call, const char *workload = "mono") {
    if (check_only)
        return;
    for (bool serial : {true, false})
        for (unsigned rep = 0; rep < repeats; ++rep) {
            std::array<unsigned, 8> cursor{0, 7, 13, 21, 32, 41, 54, 63};
            for (unsigned i = 0; i < 512; ++i)
                for (unsigned j = 0; j < 8; ++j)
                    cursor[j] = next(call(cursor[j], j), i);
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                if (serial)
                    cursor[0] = next(call(cursor[0], 0), i);
                else
                    sp::detail::each<8>([&](auto j) { cursor[j] = next(call(cursor[j], j), i); });
            }
            auto stop = std::chrono::steady_clock::now();
            for (auto c : cursor)
                observed += c;
            auto ns = std::chrono::duration<double, std::nano>(stop - start).count() /
                      (double(iterations) * (serial ? 1 : 8));
            std::printf("%s,%u,%u,%u,%s,%s,%u,%.6f\n", workload, fixture.width, op,
                        unsigned(active), name, serial ? "dependent" : "independent8", rep, ns);
        }
}
template <unsigned K>
[[gnu::always_inline]] auto decode(void *opaque, std::size_t row, std::uint16_t active,
                                   Packet<sizeof(sp::uint_for<K>)>, unsigned) ->
    typename Chain<sizeof(sp::uint_for<K>)>::result {
    const Source<K> source{static_cast<State *>(opaque)->bytes, windows * 16};
    sp::composition::native_ops ops;
    auto x = sp::composition::read(ops, sp::composition::describe(source), row, active);
    return {Packet<sizeof(sp::uint_for<K>)>{x.v}, active};
}
template <unsigned B, C Op>
[[gnu::always_inline]] auto compare(void *opaque, std::size_t, std::uint16_t active, Packet<B> x,
                                    unsigned) -> typename Chain<B>::result {
    return {x, sp::filter_values<Op>(x, static_cast<State *>(opaque)->query, active)};
}
template <unsigned B>
[[gnu::always_inline]] void finish(void *opaque, std::size_t, std::uint16_t mask, Packet<B>) {
    static_cast<State *>(opaque)->result = mask;
}
template <unsigned B>
[[gnu::noinline]] std::uint16_t shared_entry(State &state, std::size_t row, word q,
                                             std::uint16_t active) {
    if (!active)
        return 0;
    state.query = q;
    static_cast<const Chain<B> *>(state.plan)->run(&state, row, active, Packet<B>{});
    return state.result;
}
template <unsigned K, C Op>
[[gnu::always_inline]] std::uint16_t inline_fused(const std::uint8_t *bytes, std::size_t row,
                                                  word q, std::uint16_t active) {
    const Source<K> source{bytes, windows * 16};
    return sp::composition::filter16_unchecked<Op>(sp::composition::describe(source), row, q,
                                                   active);
}
template <unsigned K, C Op>
[[gnu::noinline]] std::uint16_t fused_entry(State &state, std::size_t row, word q,
                                            std::uint16_t active) {
    return inline_fused<K, Op>(state.bytes, row, q, active);
}
template <unsigned K, C Op>
[[gnu::always_inline]] auto fused_stage(void *opaque, std::size_t row, std::uint16_t active,
                                        Packet<sizeof(sp::uint_for<K>)> x, unsigned) ->
    typename Chain<sizeof(sp::uint_for<K>)>::result {
    auto &s = *static_cast<State *>(opaque);
    return {x, inline_fused<K, Op>(s.bytes, row, s.query, active)};
}
template <unsigned K, C Op>
constexpr bool hot =
    (K < 8 && sp::detail::use_packed_filter<Op, K>) || K == 8 || K == 16 || K == 32 || K == 64;

template <unsigned K, C Op> void run_case(const Fixture &fixture) {
    constexpr unsigned B = sizeof(sp::uint_for<K>);
    static const auto shared = *Chain<B>::prepare(
        std::array<typename Chain<B>::function, 2>{&Chain<B>::template stage<decode<K>>,
                                                   &Chain<B>::template stage<compare<B, Op>>},
        &Chain<B>::template completion<finish<B>>);
    static const auto one = *Chain<B>::prepare(
        std::array<typename Chain<B>::function, 1>{&Chain<B>::template stage<fused_stage<K, Op>>},
        &Chain<B>::template completion<finish<B>>);
    State s{fixture.bytes.data(), &shared};
    verify(&fused_entry<K, Op>, s, fixture, unsigned(Op));
    verify(&shared_entry<B>, s, fixture, unsigned(Op));
    s.plan = &one;
    verify(&shared_entry<B>, s, fixture, unsigned(Op));
    catalogue.push_back({&fused_entry<K, Op>,
                         &shared_entry<B>,
                         &shared_entry<B>,
                         hot<K, Op> ? &fused_entry<K, Op> : &shared_entry<B>,
                         {fixture.bytes.data(), &shared},
                         &one,
                         fixture.queries.data(),
                         K,
                         unsigned(Op)});
    for (std::uint16_t active : {std::uint16_t(65535), std::uint16_t(0xa53c)}) {
        auto direct = [&](unsigned w, unsigned) __attribute__((always_inline)) {
            asm("" : "+r"(w));
            return inline_fused<K, Op>(fixture.bytes.data(), std::size_t(w) * 16,
                                       fixture.queries[w], active);
        };
        measure(fixture, unsigned(Op), "inline_fused", active, direct);
        if constexpr (K < 8) {
            auto decoded = [&](unsigned w, unsigned) __attribute__((always_inline)) {
                asm("" : "+r"(w));
                const Source<K> source{fixture.bytes.data(), windows * 16};
                sp::composition::native_ops ops;
                auto value = sp::composition::read(ops, sp::composition::describe(source),
                                                   std::size_t(w) * 16, active);
                return sp::filter_values<Op>(value, fixture.queries[w], active);
            };
            for (unsigned w = 0; w < windows; ++w)
                assert(decoded(w, 0) == direct(w, 0));
            measure(fixture, unsigned(Op), "inline_decoded", active, decoded);
        }
        s.plan = &shared;
        measure_bound(&fused_entry<K, Op>, s, fixture, unsigned(Op), "bound_fused", active);
        measure_bound(&shared_entry<B>, s, fixture, unsigned(Op), "shared", active);
        s.plan = &one;
        measure_bound(&shared_entry<B>, s, fixture, unsigned(Op), "one_stage", active);
    }
}
template <unsigned K> void run_width() {
    // Keep fixtures alive through the mixed-catalogue measurements.
    static const Fixture fixture(K);
    run_case<K, C::equal>(fixture);
    run_case<K, C::less>(fixture);
    run_case<K, C::less_equal>(fixture);
    run_case<K, C::greater>(fixture);
    run_case<K, C::greater_equal>(fixture);
}
} // namespace runtime
