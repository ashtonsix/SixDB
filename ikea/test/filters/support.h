#pragma once
#include <ikea/seriespack.h>
#include <ikea/seriespack/author/read.h>
#include <ikea/tuplepack.h>
#include "../scope.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
using C = ikea::comparison;
template <C... Ops> struct operations {};
inline bool oracle(C op, std::uint64_t a, std::uint64_t b) {
    switch (op) {
    case C::equal:
        return a == b;
    case C::less:
        return a < b;
    case C::less_equal:
        return a <= b;
    case C::greater:
        return a > b;
    case C::greater_equal:
        return a >= b;
    }
    __builtin_unreachable();
}
template <class F, C Op> void series_case() {
    constexpr unsigned n = 83;
    alignas(64) std::array<std::uint8_t, 16384> payload_storage{}, heads{}, heads2{};
    auto payload = std::span(payload_storage).subspan(1);
    auto v = *sp::view<F, std::uint8_t>::attach(
        n,
        {{{payload, F::tile_bytes + 64}, {heads, F::tile_rows + 8}, {heads2, F::tile_rows + 16}}});
    std::array<std::uint64_t, n> truth;
    std::mt19937_64 random(F::width + F::heads);
    constexpr auto limit = ~std::uint64_t{0} >> (64 - F::width);
    for (unsigned i = 0; i < n; ++i) {
        truth[i] = (i % 5 == 0 ? 0 : i % 5 == 1 ? limit : random() & limit);
        sp::set_unchecked(v, i, truth[i]);
    }
    const auto composed = *sp::composition::prepare(sp::composition::describe(v), n);
    const auto guarded = [&] {
        if constexpr (F::heads == 8 && F::width > 8)
            return *sp::composition::prepare(
                sp::composition::guarded(sp::composition::describe(v)), n);
        else
            return composed;
    }();
    for (auto rhs :
         {std::uint64_t{0}, std::uint64_t{1}, limit / 2, limit, limit + 1, ~std::uint64_t{0}})
        for (unsigned first : {0u, 1u, 8u, 16u, 31u, 63u, 83u})
            for (unsigned count : {0u, 1u, 8u, 15u, 16u, 17u, 24u, 32u, 40u, 48u, 56u, 64u}) {
                if (count > n - first)
                    continue;
                for (auto active :
                     {std::uint64_t{0}, ~std::uint64_t{0}, std::uint64_t{0xa53c09180001800f}}) {
                    std::uint64_t wanted = 0;
                    for (unsigned i = 0; i < count; ++i)
                        if ((active >> i & 1) && oracle(Op, truth[first + i], rhs))
                            wanted |= std::uint64_t{1} << i;
                    assert(sp::filter<Op>(v, first, count, rhs, active) == wanted);
                    assert(composed.template filter<Op>(first, count, rhs, active) == wanted);
                    assert(guarded.template filter<Op>(first, count, rhs, active) == wanted);
                    if constexpr (Op == C::equal) {
                        unsigned rank = count;
                        for (unsigned i = 0; i < count; ++i)
                            if (((active >> i) & 1) && truth[first + i] == rhs) {
                                rank = i;
                                break;
                            }
                        assert(sp::indexof(v, first, count, rhs, active) == rank);
                        assert(composed.indexof(first, count, rhs, active) == rank);
                        assert(guarded.indexof(first, count, rhs, active) == rank);
                    }
                }
            }
#if defined(__aarch64__) || defined(__AVX2__)
    // Normalization removes K from a downstream consumer, including its query
    // domain shortcut. Full-width queries must still have the same meaning.
    sp::composition::native_ops ops;
    const auto decoded = sp::composition::read(ops, sp::composition::describe(v), 0, 0xffff);
    const sp::native::values<8 * sizeof(sp::uint_for<F::width>)> canonical{decoded.v};
    for (auto rhs : {std::uint64_t{0}, limit / 2, limit, limit + 1, ~std::uint64_t{0}}) {
        unsigned expected = 0;
        for (unsigned i = 0; i < 16; ++i)
            if ((0xa53c >> i & 1) && oracle(Op, truth[i], rhs))
                expected |= 1u << i;
        assert(sp::filter_values<Op>(canonical, rhs, 0xa53c) == expected);
    }
#endif
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    for (auto rhs : {std::uint64_t{0}, limit / 2, limit, ~std::uint64_t{0}}) {
        sp::composition::wide_ops<64> ops;
        auto native =
            sp::composition::read(ops, sp::composition::describe(v), 0, ~std::uint64_t{0});
        auto got = sp::filter_values<Op>(native, rhs, 0xfedcba9876543210ULL);
        std::uint64_t expected = 0;
        for (unsigned i = 0; i < 64; ++i)
            if ((0xfedcba9876543210ULL >> i & 1) && oracle(Op, truth[i], rhs))
                expected |= std::uint64_t{1} << i;
        assert(got == expected);
    }
#endif
    assert(!sp::filter<Op>(v, 84, 0, 0));
    assert(!sp::filter<Op>(v, 0, 65, 0));
    assert(!sp::indexof(v, 84, 0, 0));
    assert(!sp::indexof(v, 0, 65, 0));
    assert(!composed.indexof(84, 0, 0));
    assert(!guarded.template filter<Op>(84, 0, 0));
    assert(!guarded.template filter<Op>(0, 65, 0));
    if constexpr (F::width < 8 && F::storage == sp::geometry::local)
        for (unsigned q = 0; q <= (1u << F::width); ++q)
            for (unsigned i = 0; i < 80; i += 8) {
                unsigned want = 0;
                for (unsigned j = 0; j < 8; ++j)
                    want |= unsigned(oracle(Op, truth[i + j], q)) << j;
                assert((sp::detail::filter_local8<Op, F::width>(
                            payload.data() + i / 8 * (F::tile_bytes + 64), q) == want));
            }
}
template <unsigned K> void width() {
    if constexpr (ikea::test::sampled_width(K))
    [&]<C... Ops>(operations<Ops...>) {
        (series_case<sp::format<K>, Ops>(), ...);
        if constexpr (sp::striped_width(K))
            (series_case<sp::format<K, sp::geometry::striped>, Ops>(), ...);
        if constexpr (K >= 8)
            (series_case<sp::format<K, sp::geometry::local, 8>, Ops>(), ...);
        if constexpr (K >= 16)
            (series_case<sp::format<K, sp::geometry::local, 16>, Ops>(), ...);
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
}
template <unsigned N, unsigned Rows, C Op> void tuple_case() {
    std::array<tp::code, 2> codes{{{0, 3, 5}, {0, 0, 3}}};
    auto layout = *tp::layout::make(1, codes);
    std::array<tp::byte, 129> bytes;
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = i * 71;
    auto source = *tp::view::bind(layout, bytes, 65, 2);
    auto plan = *tp::reader<N, Rows>::make(layout, std::array<tp::byte, 1>{0});
    auto read = *tp::bind_reader(plan, source);
    auto filter = *tp::bind_filter(read);
    constexpr auto live = ~std::uint64_t{0} >> (64 - Rows);
    for (auto rhs : {std::uint64_t{0}, std::uint64_t{15}, std::uint64_t{31}, std::uint64_t{32},
                     ~std::uint64_t{0}})
        for (auto active : {std::uint64_t{0}, live, live & std::uint64_t{0xa53c09180001800f}}) {
            std::uint64_t expected = 0;
            for (unsigned i = 0; i < Rows; ++i)
                if ((active >> i & 1) && oracle(Op, bytes[i * 2] >> 3, rhs))
                    expected |= std::uint64_t{1} << i;
            assert(filter.template filter<Op>(0, rhs, active) == expected);
        }
    assert(filter.template filter<Op>(65, 0, 0) == 0);
    assert(!filter.template filter<Op>(65, 0, 1));
    auto hole = *tp::reader<N, Rows>::make(layout, std::array<tp::byte, 1>{tp::hole});
    auto hole_read = *tp::bind_reader(hole, source);
    assert(!tp::bind_filter(hole_read));
}
