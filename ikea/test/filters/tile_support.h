#pragma once
#include "support.h"

template <unsigned K> void tile_boundary_case(std::uint8_t* end) {
    using F = sp::format<K>;
    constexpr auto limit = ~std::uint64_t{0} >> (64 - K);
    for (unsigned n : {8u, 16u, 24u, 32u, 40u, 48u, 56u, 64u}) {
        auto* bytes = end - n * K / 8;
        std::fill_n(bytes, n * K / 8, 0);
        auto admitted = sp::view<F, std::uint8_t>::attach(n, {{{{bytes, n * K / 8}, K}, {}, {}}});
        assert(admitted);
        std::array<std::uint64_t, 64> expected{};
        for (unsigned i = 0; i < n; ++i) {
            expected[i] = i % 3 ? ((std::uint64_t(i) * 0x918173179ULL) & limit) : limit;
            sp::set_unchecked(*admitted, i, expected[i]);
        }
        // Actual exact-bound native consumers at an inaccessible page.
        for (auto rhs : {std::uint64_t{0}, limit / 2, limit, ~std::uint64_t{0}})
            for (auto active : {~std::uint64_t{0}, std::uint64_t{0x739a51000a05f17c}}) {
                std::uint64_t eq = 0, lt = 0;
                for (unsigned i = 0; i < n; ++i)
                    if ((active >> i) & 1) {
                        eq |= std::uint64_t(expected[i] == rhs) << i;
                        lt |= std::uint64_t(expected[i] < rhs) << i;
                    }
                assert(sp::filter<C::equal>(*admitted, 0, n, rhs, active) == eq);
                assert(sp::filter<C::less>(*admitted, 0, n, rhs, active) == lt);
                const auto dense = sp::dense(*admitted);
                assert(dense);
                assert(sp::filter<C::equal>(*dense, 0, n, rhs, active) == eq);
                unsigned rank = n;
                for (unsigned i = 0; i < n; ++i)
                    if (((active >> i) & 1) && expected[i] == rhs) { rank = i; break; }
                assert(sp::indexof(*admitted, 0, n, rhs, active) == rank);
                assert(sp::indexof(*dense, 0, n, rhs, active) == rank);
                assert(sp::filter<C::less>(*dense, 0, n, rhs, active) == lt);
            }
    }
}
template <unsigned K> void tile_boundary(std::uint8_t* end) {
    if constexpr (ikea::test::sampled_width(K))
        tile_boundary_case<K>(end);
}
