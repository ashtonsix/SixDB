#pragma once
#include <ikea/seriespack/detail/native/neon/read.h>

namespace ikea::seriespack::neon_group {
template <unsigned K, unsigned N> struct values {
    static_assert(N == 16 || N == 32 || N == 64);
    std::array<neon::values<K>, N / 16> block;
};
template <unsigned K, unsigned N> using selection = values<K, N>;
using sum_state = neon::sum_state;
using neon::shifted;

template <unsigned K, unsigned N, class Reader>
[[gnu::always_inline]] inline values<K, N> packets(Reader&& read) {
    values<K, N> out;
    detail::each<N / 16>([&](auto p) { out.block[p] = read(p * 16); });
    return out;
}
template <class F, bool Dense, unsigned N>
[[gnu::always_inline]] inline values<F::tail, N> tail(const std::uint8_t* base, std::size_t stride,
                                                      std::size_t i) {
    constexpr unsigned R = F::tail;
    if constexpr (N == 64 && Dense && F::storage == geometry::local && F::body == 0) {
        __builtin_assume(i % 16 == 0);
        constexpr unsigned chunks = (8 * R + 15) / 16;
        std::array<uint8x16_t, chunks> raw;
        detail::each<chunks>([&](auto c) {
            raw[c] =
                neon::load128<std::min<unsigned>(16, 8 * R - c * 16)>(base + i / 8 * R + c * 16);
        });
        values<R, N> out;
        if constexpr (R <= 2)
            detail::each<4>([&](auto pair) {
                constexpr unsigned first = pair * 2 * R / 16, last = ((pair + 1) * 2 * R - 1) / 16;
                auto table = [&](const std::array<std::uint8_t, 16>& map) {
                    if constexpr (first == last)
                        return vqtbl1q_u8(raw[first], vld1q_u8(map.data()));
                    else
                        return vqtbl2q_u8({{raw[first], raw[last]}}, vld1q_u8(map.data()));
                };
                static constexpr std::array<std::uint8_t, 16> bits{1, 2, 4, 8, 16, 32, 64, 128,
                                                                   1, 2, 4, 8, 16, 32, 64, 128};
                auto x = vdupq_n_u8(0);
                detail::each<R>([&](auto b) {
                    static constexpr auto map = [] {
                        std::array<std::uint8_t, 16> a{};
                        for (unsigned j = 0; j < 16; ++j)
                            a[j] = (decltype(pair)::value * 2 + j / 8) * R + decltype(b)::value -
                                   first * 16;
                        return a;
                    }();
                    x = vorrq_u8(x, vandq_u8(vtstq_u8(table(map), vld1q_u8(bits.data())),
                                             vdupq_n_u8(1u << b)));
                });
                out.block[pair].v[0] = x;
            });
        else
            // Gather planes 0..3 (and 4..7) of four tiles into 32-bit lanes.
            // Two lane-local transpose stages leave each byte holding one row
            // split across nibbles; shift-inserts finish the third stage and
            // zips restore row order. Planes 4..7 are absent below five bits.
            detail::each<2>([&](auto quad) {
                constexpr unsigned q = decltype(quad)::value;
                auto gather = [&]<unsigned Plane>() {
                    constexpr unsigned lo = q * 4 * R + Plane,
                                       hi = (q * 4 + 3) * R + std::min(R, Plane + 4) - 1;
                    constexpr unsigned first = lo / 16, count = hi / 16 - first + 1;
                    static constexpr auto map = [] {
                        std::array<std::uint8_t, 16> a{};
                        a.fill(255);
                        for (unsigned t = 0; t < 4; ++t)
                            for (unsigned b = 0; b < 4 && Plane + b < R; ++b)
                                a[t * 4 + b] = (q * 4 + t) * R + Plane + b - first * 16;
                        return a;
                    }();
                    const auto index = vld1q_u8(map.data());
                    uint8x16_t x;
                    if constexpr (count == 1)
                        x = vqtbl1q_u8(raw[first], index);
                    else if constexpr (count == 2)
                        x = vqtbl2q_u8({{raw[first], raw[first + 1]}}, index);
                    else
                        x = vqtbl3q_u8({{raw[first], raw[first + 1], raw[first + 2]}}, index);
                    return vreinterpretq_u8_u64(neon::transpose_quads(vreinterpretq_u64_u8(x)));
                };
                const auto low = gather.template operator()<0>();
                uint8x16_t rows03, rows47;
                if constexpr (R > 4) {
                    const auto high = gather.template operator()<4>();
                    rows03 = vsliq_n_u8(low, high, 4);
                    rows47 = vsriq_n_u8(high, low, 4);
                } else {
                    rows03 = vandq_u8(low, vdupq_n_u8(15));
                    rows47 = vshrq_n_u8(low, 4);
                }
                const auto a = vreinterpretq_u32_u8(rows03), b = vreinterpretq_u32_u8(rows47);
                out.block[q * 2].v[0] = vreinterpretq_u8_u32(vzip1q_u32(a, b));
                out.block[q * 2 + 1].v[0] = vreinterpretq_u8_u32(vzip2q_u32(a, b));
            });
        return out;
    } else if constexpr (N % 32 == 0 && F::storage == geometry::local && F::body != 0 && R >= 3) {
        // Mixed residuals sit after each tile's body, so gather four tiles by
        // loading the word that ends at each residual (it never leaves the tile)
        // into a 32-bit lane, or a 64-bit lane when R > 4, then share the
        // quad transpose with dense pure tails.
        __builtin_assume(i % 16 == 0);
        const std::size_t step = Dense ? F::tile_bytes : stride;
        values<R, N> out;
        detail::each<N / 32>([&](auto quad) {
            constexpr unsigned q = decltype(quad)::value;
            const auto* p = base + (i / 8 + q * 4) * step + 8 * F::body;
            uint8x16_t low, high;
            if constexpr (R <= 4) {
                auto x = vdupq_n_u32(0);
                detail::each<4>([&](auto t) {
                    x = vsetq_lane_u32(static_cast<std::uint32_t>(detail::load<4>(p + t * step + R - 4)),
                                       x, t);
                });
                if constexpr (R < 4)
                    x = vshrq_n_u32(x, 8 * (4 - R));
                low = vreinterpretq_u8_u64(neon::transpose_quads(vreinterpretq_u64_u32(x)));
            } else {
                auto word = [&](unsigned t) { return detail::load<8>(p + t * step + R - 8); };
                const auto a = vshrq_n_u64(vcombine_u64(vcreate_u64(word(0)), vcreate_u64(word(1))),
                                           8 * (8 - R));
                const auto b = vshrq_n_u64(vcombine_u64(vcreate_u64(word(2)), vcreate_u64(word(3))),
                                           8 * (8 - R));
                const auto x = vreinterpretq_u32_u64(a), y = vreinterpretq_u32_u64(b);
                low = vreinterpretq_u8_u64(
                    neon::transpose_quads(vreinterpretq_u64_u32(vuzp1q_u32(x, y))));
                high = vreinterpretq_u8_u64(
                    neon::transpose_quads(vreinterpretq_u64_u32(vuzp2q_u32(x, y))));
            }
            uint8x16_t rows03, rows47;
            if constexpr (R > 4) {
                rows03 = vsliq_n_u8(low, high, 4);
                rows47 = vsriq_n_u8(high, low, 4);
            } else {
                rows03 = vandq_u8(low, vdupq_n_u8(15));
                rows47 = vshrq_n_u8(low, 4);
            }
            const auto a = vreinterpretq_u32_u8(rows03), b = vreinterpretq_u32_u8(rows47);
            out.block[q * 2].v[0] = vreinterpretq_u8_u32(vzip1q_u32(a, b));
            out.block[q * 2 + 1].v[0] = vreinterpretq_u8_u32(vzip2q_u32(a, b));
        });
        return out;
    } else
        return packets<R, N>([&](unsigned offset) {
            return neon::values<R>{{neon::tail16<F, Dense>(base, stride, i + offset)}};
        });
}
template <class F, bool Dense, unsigned N>
[[gnu::always_inline]] inline values<F::body * 8, N> body(const std::uint8_t* base,
                                                          std::size_t stride, std::size_t i) {
    return packets<F::body * 8, N>(
        [&](unsigned offset) { return neon::body16<F, Dense>(base, stride, i + offset); });
}
template <unsigned Shift, unsigned A, unsigned B, unsigned N>
[[gnu::always_inline]] inline values<A + Shift, N> join(values<A, N> high, values<B, N> low) {
    values<A + Shift, N> out;
    detail::each<N / 16>(
        [&](auto p) { out.block[p] = neon::join<Shift>(high.block[p], low.block[p]); });
    return out;
}
template <class F, bool Dense, unsigned N>
[[gnu::always_inline]] inline values<F::payload, N> read(const std::uint8_t* base,
                                                         std::size_t stride, std::size_t i) {
    if constexpr (F::body == 0)
        return tail<F, Dense, N>(base, stride, i);
    else if constexpr (F::tail == 0)
        return body<F, Dense, N>(base, stride, i);
    else
        return join<F::tail>(body<F, Dense, N>(base, stride, i),
                             tail<F, Dense, N>(base, stride, i));
}
template <class U, unsigned K, unsigned N>
[[gnu::always_inline]] inline void store(U* __restrict out, values<K, N> x) {
    detail::each<N / 16>([&](auto p) { neon::store16(out + p * 16, x.block[p]); });
}
template <unsigned K, unsigned N>
[[gnu::always_inline]] inline selection<K, N> active_mask(std::uint64_t active) {
    selection<K, N> out;
    detail::each<N / 16>(
        [&](auto p) { out.block[p] = neon::mask16<K>(static_cast<std::uint16_t>(active >> (p * 16))); });
    return out;
}
template <unsigned K, unsigned N>
[[gnu::always_inline]] inline selection<K, N> less(values<K, N> x, std::uint64_t cutoff,
                                                   std::uint64_t active) {
    selection<K, N> out;
    detail::each<N / 16>([&](auto p) {
        out.block[p] =
            neon::less(x.block[p], cutoff, static_cast<std::uint16_t>(active >> (p * 16)));
    });
    return out;
}
template <unsigned K, unsigned N>
[[gnu::always_inline]] inline sum_state sum(values<K, N> x, selection<K, N> mask) {
    if constexpr (N == 16)
        return neon::sum(x.block[0], mask.block[0]);
    constexpr unsigned L = sizeof(uint_for<K>);
    sum_state out;
    // Bound intermediate lanes across this finite group, then widen before
    // adding to the unbounded range accumulator. No per-packet scalar result.
    auto small16 = vdupq_n_u16(0);
    auto small32 = vdupq_n_u32(0);
    detail::each<N / 16>([&](auto p) {
        detail::each<L>([&](auto j) {
            const auto kept = vandq_u8(x.block[p].v[j], mask.block[p].v[j]);
            if constexpr (L == 1)
                small16 = vaddq_u16(small16, vpaddlq_u8(kept));
            if constexpr (L == 2)
                small32 = vaddq_u32(small32, vpaddlq_u16(vreinterpretq_u16_u8(kept)));
            if constexpr (L == 4)
                out.value = vaddq_u64(out.value, vpaddlq_u32(vreinterpretq_u32_u8(kept)));
            if constexpr (L == 8)
                out.value = vaddq_u64(out.value, vreinterpretq_u64_u8(kept));
        });
    });
    if constexpr (L == 1)
        out.value = vpaddlq_u32(vpaddlq_u16(small16));
    if constexpr (L == 2)
        out.value = vpaddlq_u32(small32);
    return out;
}
} // namespace ikea::seriespack::neon_group
