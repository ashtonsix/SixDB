#include <ikea/tuplepack/author/execution.h>
#pragma once
#include "runtime.h"
#include <ikea/tuplepack.h>
#include <cstdlib>
#include <string>
namespace extras {
using namespace runtime;
namespace tp = ikea::tuplepack;
struct Context {
    const void *input;
    const void *chain;
    word lo{}, hi{};
    std::uint16_t result{};
};
using Call = std::uint16_t (*)(Context &, std::size_t, word, word, std::uint16_t);
template <unsigned B>
[[gnu::noinline]] std::uint16_t entry(Context &s, std::size_t row, word lo, word hi,
                                      std::uint16_t active) {
    if (!active)
        return 0;
    s.lo = lo;
    s.hi = hi;
    static_cast<const Chain<B> *>(s.chain)->run(&s, row, active, Packet<B>{});
    return s.result;
}
template <unsigned B>
[[gnu::always_inline]] void finish(void *p, std::size_t, std::uint16_t active, Packet<B>) {
    static_cast<Context *>(p)->result = active;
}
template <class Spec>
[[gnu::always_inline]] auto decode(void *p, std::size_t row, std::uint16_t active, Packet<Spec::B>,
                                   unsigned) -> typename Chain<Spec::B>::result {
    return {
        Spec::decoded(*static_cast<const typename Spec::Input *>(static_cast<Context *>(p)->input),
                      row, active),
        active};
}
template <unsigned B, bool Pair, C Op>
[[gnu::always_inline]] auto joined(void *p, std::size_t, std::uint16_t active, Packet<B> value,
                                   unsigned) -> typename Chain<B>::result {
    auto &s = *static_cast<Context *>(p);
    if constexpr (Pair)
        return {value, std::uint16_t(sp::author::filter_values<C::greater_equal>(value, s.lo, active) &
                                     sp::author::filter_values<C::less>(value, s.hi, active))};
    else
        return {value, sp::author::filter_values<Op>(value, s.lo, active)};
}
template <unsigned B, bool Upper>
[[gnu::always_inline]] auto separate(void *p, std::size_t, std::uint16_t active, Packet<B> value,
                                     unsigned) -> typename Chain<B>::result {
    auto &s = *static_cast<Context *>(p);
    if constexpr (Upper)
        return {value, sp::author::filter_values<C::less>(value, s.hi, active)};
    else
        return {value, sp::author::filter_values<C::greater_equal>(value, s.lo, active)};
}
template <class Spec>
[[gnu::noinline]] std::uint16_t bound(Context &s, std::size_t row, word lo, word hi,
                                      std::uint16_t active) {
    return Spec::fused(*static_cast<const typename Spec::Input *>(s.input), row, lo, hi, active);
}
template <class Spec>
[[gnu::always_inline]] auto one(void *p, std::size_t row, std::uint16_t active,
                                Packet<Spec::B> value, unsigned) ->
    typename Chain<Spec::B>::result {
    auto &s = *static_cast<Context *>(p);
    return {value, Spec::fused(*static_cast<const typename Spec::Input *>(s.input), row, s.lo, s.hi,
                               active)};
}
template <unsigned K> struct Pair {
    using Input = Fixture;
    using Decoder = Pair<K>;
    static constexpr unsigned B = sizeof(sp::uint_for<K>), width = K;
    static constexpr bool pair = true;
    static constexpr C op = C::equal;
    [[gnu::always_inline]] static Packet<B> decoded(const Input &in, std::size_t row,
                                                    std::uint16_t active) {
        Source<K> source{std::as_bytes(std::span{in.bytes}).data(), windows * 16};
        sp::author::native_ops ops;
        auto x = sp::author::read(ops, sp::author::expression(source), row, active);
        return {x.v};
    }
    [[gnu::always_inline]] static std::uint16_t fused(const Input &in, std::size_t row, word lo,
                                                      word hi, std::uint16_t active) {
        if constexpr (K < 8 && sp::detail::use_packed_filter<C::less, K>)
            return inline_fused<K, C::greater_equal>(in.bytes.data(), row, lo, active) &
                   inline_fused<K, C::less>(in.bytes.data(), row, hi, active);
        else {
            auto v = decoded(in, row, active);
            return sp::author::filter_values<C::greater_equal>(v, lo, active) &
                   sp::author::filter_values<C::less>(v, hi, active);
        }
    }
    static word truth(const Input &in, unsigned i) { return in.truth[i]; }
};
using Reader = tp::author::native_reader<64, std::byte, 16>;
using Filter = typename decltype(tp::bind_predicate(std::declval<const tp::reader<64, std::byte, 16>&>()))::value_type;
struct TupleInput {
    const Reader *read;
    const Filter *filter;
    const tp::view *view;
    unsigned shift, width;
};
struct TupleDecode {
    using Input = TupleInput;
    static constexpr unsigned B = 1;
    [[gnu::always_inline]] static Packet<1> decoded(const Input &in, std::size_t row,
                                                    std::uint16_t active) {
        auto x = in.read->get_unchecked(row, active);
#if defined(__aarch64__)
        return {{x.a}};
#elif defined(__AVX512VBMI__)
        return {{_mm512_castsi512_si128(x)}};
#else
        return {{_mm256_castsi256_si128(x.a)}};
#endif
    }
};
template <unsigned Width, C Op> struct Tuple : TupleDecode {
    using Decoder = TupleDecode;
    static constexpr unsigned width = Width;
    static constexpr bool pair = false;
    static constexpr C op = Op;
    [[gnu::always_inline]] static std::uint16_t fused(const Input &in, std::size_t row, word lo,
                                                      word, std::uint16_t active) {
        return in.filter->template filter_unchecked<Op>(row, lo, active);
    }
    static word truth(const Input &in, unsigned i) {
        return (std::to_integer<unsigned>(in.view->row_unchecked(i)[0]) >> in.shift) & ((1u << in.width) - 1);
    }
};
void measure_bound_extra(Call, Context, const Fixture &, const std::array<word, windows> &,
                         unsigned, const char *, std::uint16_t, const char *);
template <class Spec> void run_extra(const typename Spec::Input &input, const char *workload) {
    constexpr unsigned B = Spec::B;
    Fixture fixture(Spec::width);
    const word limit = ~word{0} >> (64 - Spec::width);
    std::array<word, windows> upper;
    for (unsigned w = 0; w < windows; ++w)
        upper[w] =
            fixture.queries[w] + std::min(limit - fixture.queries[w], std::max(word{1}, limit / 4));
    const auto shared = *Chain<B>::make(
        std::array<typename Chain<B>::function, 2>{
            &Chain<B>::template stage<decode<typename Spec::Decoder>>,
            &Chain<B>::template stage<joined<B, Spec::pair, Spec::op>>},
        &Chain<B>::template completion<finish<B>>);
    const auto single = *Chain<B>::make(
        std::array<typename Chain<B>::function, 1>{&Chain<B>::template stage<one<Spec>>},
        &Chain<B>::template completion<finish<B>>);
    const auto split = *Chain<B>::make(
        std::array<typename Chain<B>::function, 3>{
            &Chain<B>::template stage<decode<typename Spec::Decoder>>,
            &Chain<B>::template stage<separate<B, false>>,
            &Chain<B>::template stage<separate<B, true>>},
        &Chain<B>::template completion<finish<B>>);
    struct Choice {
        const char *name;
        Call call;
        const void *chain;
    };
    std::vector<Choice> choices{{"bound_fused", &bound<Spec>, &single},
                                {"one_stage", &entry<B>, &single},
                                {"shared", &entry<B>, &shared}};
    if constexpr (Spec::pair)
        choices.push_back({"shared_split", &entry<B>, &split});
    for (auto &choice : choices) {
        Context s{&input, choice.chain};
        for (word q : {word{0}, word{1}, limit / 2, limit, limit + 1, ~word{0}})
            for (word h : {word{0}, limit / 2, limit, ~word{0}})
                for (unsigned w : {0u, 7u, 63u})
                    for (std::uint16_t a :
                         {std::uint16_t(0), std::uint16_t(0xa53c), std::uint16_t(65535)}) {
                        unsigned expected = 0;
                        for (unsigned i = 0; i < 16; ++i)
                            if (a >> i & 1) {
                                auto x = Spec::truth(input, w * 16 + i);
                                bool ok = Spec::pair ? (x >= q && x < h)
                                                     : oracle(unsigned(Spec::op), x, q);
                                expected |= unsigned(ok) << i;
                            }
                        assert(choice.call(s, w * 16, q, h, a) == expected);
                    }
    }
    for (std::uint16_t active : {std::uint16_t(65535), std::uint16_t(0xa53c)}) {
        auto direct = [&](unsigned w, unsigned) __attribute__((always_inline)) {
            asm("" : "+r"(w));
            return Spec::fused(input, std::size_t(w) * 16, fixture.queries[w], upper[w], active);
        };
        measure(fixture, unsigned(Spec::op), "inline_fused", active, direct, workload);
        for (auto &choice : choices) {
            measure_bound_extra(choice.call, {&input, choice.chain}, fixture, upper,
                                unsigned(Spec::op), choice.name, active, workload);
        }
    }
}
template <unsigned K> void run_pair() {
    Fixture fixture(K);
    run_extra<Pair<K>>(fixture, "pair");
}
template <unsigned Width, unsigned Shift, unsigned Stride, C Op> void run_tuple() {
    auto layout = *tp::layout::make(1, std::array<tp::code, 1>{{{0, Shift, Width}}});
    std::vector<std::uint8_t> bytes(windows * 16 * Stride);
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = (i * 71 + (i >> 2) * 43) & 255;
    auto view = *tp::view::attach(layout, std::as_writable_bytes(std::span{bytes}), windows * 16, Stride);
    auto plan = *tp::read_plan<64, 16>::make(layout, std::array<std::uint8_t, 1>{0});
    auto operation = *tp::bind_reader(plan, view);
    Reader read(operation);
    auto filter = *tp::bind_predicate(operation);
    TupleInput input{&read, &filter, &view, Shift, Width};
    const char *label = Stride == 1 ? "tuple_tight" : "tuple_strided";
    run_extra<Tuple<Width, Op>>(input, label);
}
} // namespace extras
