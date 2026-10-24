#pragma once
#include <ikea/seriespack/layout.h>
#include <ikea/seriespack/detail/wire.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX512BW__)
#include <immintrin.h>
#endif

namespace ikea::seriespack::detail {
/// Unconditioned payload sum of consecutive dense Local tiles in one pass.
/// A sum is linear in the fields, so no value is reconstructed: residual
/// plane b contributes its popcount at 2^b and body byte j its value at
/// 2^(R + 8j).
/// Byte roles repeat every lcm(T, vector bytes) from any tile start, so each
/// vector of a period has fixed roles; popcounts accumulate per byte and are
/// weighted when flushed. Plain loads only, within the given periods.
template <class F> struct local_total {
#if defined(__aarch64__)
    static constexpr unsigned lanes = 16;
#elif defined(__AVX512BW__) && defined(__AVX512BITALG__)
    static constexpr unsigned lanes = 64;
#else
    static constexpr unsigned lanes = 0;
#endif
    static constexpr unsigned T = F::tile_bytes, B = F::body, R = F::tail;
    // NEON two- and three-byte bodies with residuals mask each body byte apart
    // and lose to the group driver (K = 17-23 locally 26-40 against 18-29 ns
    // per 256 rows); K = 24 alone gains (31 against 61).
    static constexpr bool body_route = lanes == 16 ? B <= 1 || (B == 3 && R == 0) : B <= 3;
    static constexpr bool supported =
        lanes && F::storage == geometry::local && F::heads == 0 && body_route && (R > 0 || B == 3);
    static constexpr unsigned period = supported ? std::lcm(T, lanes) : 1;
    static constexpr unsigned vectors = period / (lanes ? lanes : 1), rows = period / T * 8;
    // Periods per flush: u8 popcount lanes gain at most 8 per period, and on
    // NEON u16 body lanes at most 510 per vector.
    static constexpr unsigned flush = lanes == 16 ? std::min(31u, 128u / vectors) : 31;
    static constexpr auto weight = [] {
        std::array<std::uint8_t, period> w{};
        for (unsigned p = 0; p < period; ++p)
            if (p % T >= 8 * B)
                w[p] = static_cast<std::uint8_t>(1u << (p % T - 8 * B));
        return w;
    }();
    // Byte j of each body value.
    static constexpr auto body = [] {
        std::array<std::array<std::uint8_t, period>, B ? B : 1> m{};
        for (unsigned p = 0; p < period; ++p)
            if (p % T < 8 * B)
                m[p % T % (B ? B : 1)][p] = 0xff;
        return m;
    }();
    /// The sum of n periods (n * rows rows) from tile start p.
    static std::uint64_t run(const std::uint8_t* p, std::size_t n) requires supported {
        std::uint64_t planes = 0;
        std::array<std::uint64_t, B ? B : 1> bodies{};
#if defined(__aarch64__)
        while (n) {
            const auto m = std::min<std::size_t>(n, flush);
            std::array<uint8x16_t, vectors> acc;
            acc.fill(vdupq_n_u8(0));
            std::array<uint16x8_t, B ? B : 1> values;
            values.fill(vdupq_n_u16(0));
            for (std::size_t k = 0; k < m; ++k, p += period)
                detail::each<vectors>([&](auto v) __attribute__((always_inline)) {
                    const auto x = vld1q_u8(p + v * 16);
                    acc[v] = vaddq_u8(acc[v], vcntq_u8(x));
                    detail::each<B>([&](auto j) __attribute__((always_inline)) {
                        values[j] = vpadalq_u8(values[j], vandq_u8(x, vld1q_u8(body[j].data() + v * 16)));
                    });
                });
            auto weighted = vdupq_n_u32(0);
            detail::each<vectors>([&](auto v) __attribute__((always_inline)) {
                const auto w = vld1q_u8(weight.data() + v * 16);
                weighted = vpadalq_u16(weighted, vmull_u8(vget_low_u8(acc[v]), vget_low_u8(w)));
                weighted = vpadalq_u16(weighted, vmull_high_u8(acc[v], w));
            });
            planes += vaddlvq_u32(weighted);
            detail::each<B>([&](auto j) { bodies[j] += vaddlvq_u16(values[j]); });
            n -= m;
        }
#elif defined(__AVX512BW__) && defined(__AVX512BITALG__)
        std::array<__m512i, B ? B : 1> values;
        values.fill(_mm512_setzero_si512());
        auto weighted = _mm512_setzero_si512();
        while (n) {
            const auto m = std::min<std::size_t>(n, flush);
            std::array<__m512i, vectors> acc;
            acc.fill(_mm512_setzero_si512());
            for (std::size_t k = 0; k < m; ++k, p += period)
                detail::each<vectors>([&](auto v) __attribute__((always_inline)) {
                    const auto x = _mm512_loadu_si512(p + v * 64);
                    acc[v] = _mm512_add_epi8(acc[v], _mm512_popcnt_epi8(x));
                    detail::each<B>([&](auto j) __attribute__((always_inline)) {
                        values[j] = _mm512_add_epi64(
                            values[j], _mm512_sad_epu8(_mm512_and_si512(x, _mm512_loadu_si512(body[j].data() + v * 64)),
                                                       _mm512_setzero_si512()));
                    });
                });
            detail::each<vectors>([&](auto v) __attribute__((always_inline)) {
                const auto pairs = _mm512_maddubs_epi16(acc[v], _mm512_loadu_si512(weight.data() + v * 64));
                const auto quads = _mm512_madd_epi16(pairs, _mm512_set1_epi16(1));
                weighted = _mm512_add_epi64(weighted, _mm512_add_epi64(_mm512_and_si512(quads, _mm512_set1_epi64(0xffffffff)),
                                                                       _mm512_srli_epi64(quads, 32)));
            });
            n -= m;
        }
        planes = _mm512_reduce_add_epi64(weighted);
        detail::each<B>([&](auto j) { bodies[j] = _mm512_reduce_add_epi64(values[j]); });
#endif
        detail::each<B>([&](auto j) { planes += bodies[j] << (R + 8 * j); });
        return planes;
    }
};
/// Unconditioned payload sum of consecutive dense striped tiles in one pass.
/// Every 32-byte run of a tile is body or one residual stripe. A stripe byte
/// holds row-group bits of several planes, so its weighted bit sum is two
/// nibble table lookups (at most 191, one byte); body bytes add their value.
template <class F> struct striped_total {
#if defined(__aarch64__)
    static constexpr unsigned lanes = 16;
#elif defined(__AVX512BW__)
    static constexpr unsigned lanes = 64;
#else
    static constexpr unsigned lanes = 0;
#endif
    static constexpr unsigned T = F::tile_bytes, B = F::body, R = F::tail;
    // On AVX-512 a body-bearing period mixes roles within vectors and runs
    // slower than the group driver's field sums (Zen 5: K = 12 4.6 against
    // 3.3 ns per 256 rows), so only residual-only widths take this route.
#if defined(__aarch64__)
    static constexpr unsigned max_body = 2;
#else
    static constexpr unsigned max_body = 0;
#endif
    static constexpr bool supported =
        lanes && F::storage == geometry::striped && F::heads == 0 && B <= max_body && R > 0;
    static constexpr unsigned period = supported ? std::lcm(T, lanes) : 32, rows = period / T * F::tile_rows;
    static constexpr unsigned vectors = period / (lanes ? lanes : 32), runs = period / 32;
    // Per 32-byte run: -1 body, else the stripe it holds.
    static constexpr auto role = [] {
        std::array<int, runs> r{};
        for (unsigned k = 0; k < runs; ++k)
            r[k] = -1;
        for (unsigned s = 0; s * 8 < F::tile_rows / 32 * R; ++s)
            for (unsigned t = 0; t < period / T; ++t)
                r[(t * T + stripe_offset<F>(s)) / 32] = static_cast<int>(s);
        return r;
    }();
    // Nibble tables of a stripe: low nibble, then high nibble, 16 entries each.
    static constexpr auto nibbles = [] {
        std::array<std::array<std::uint8_t, 32>, 8> n{};
        std::array<unsigned, 64> weight{};
        for (unsigned g = 0; g < F::tile_rows / 32; ++g)
            for (unsigned b = 0; b < R; ++b)
                weight[tail_bit<R>(g, b)] = 1u << b;
        for (unsigned s = 0; s < 8; ++s)
            for (unsigned x = 0; x < 16; ++x)
                for (unsigned j = 0; j < 4; ++j)
                    if (x >> j & 1) {
                        n[s][x] += static_cast<std::uint8_t>(weight[8 * s + j]);
                        n[s][16 + x] += static_cast<std::uint8_t>(weight[8 * s + 4 + j]);
                    }
        return n;
    }();
    /// The sum of n periods (n * rows rows) from tile start p.
    static std::uint64_t run(const std::uint8_t* p, std::size_t n) requires supported {
        std::uint64_t planes = 0, low = 0, high = 0;
#if defined(__aarch64__)
        const auto nibble = vdupq_n_u8(15);
        while (n) {
            // u16 lanes gain at most 510 per vector: 64 vectors per flush.
            constexpr std::size_t flush = std::max<std::size_t>(1, 64 / vectors);
            const auto m = std::min<std::size_t>(n, flush);
            auto stripes = vdupq_n_u16(0), bodies = vdupq_n_u16(0);
            auto wide = vdupq_n_u32(0);
            for (std::size_t k = 0; k < m; ++k, p += period)
                detail::each<vectors>([&](auto v) __attribute__((always_inline)) {
                    const auto x = vld1q_u8(p + v * 16);
                    constexpr int s = role[v * 16 / 32];
                    if constexpr (s >= 0) {
                        const auto lo = vld1q_u8(nibbles[s].data()), hi = vld1q_u8(nibbles[s].data() + 16);
                        stripes = vpadalq_u8(stripes, vaddq_u8(vqtbl1q_u8(lo, vandq_u8(x, nibble)),
                                                               vqtbl1q_u8(hi, vshrq_n_u8(x, 4))));
                    } else if constexpr (B == 1)
                        bodies = vpadalq_u8(bodies, x);
                    else
                        wide = vpadalq_u16(wide, vreinterpretq_u16_u8(x));
                });
            planes += vaddlvq_u16(stripes);
            low += vaddlvq_u16(bodies) + vaddlvq_u32(wide);
            n -= m;
        }
#elif defined(__AVX512BW__)
        const auto nibble = _mm512_set1_epi8(15), zero = _mm512_setzero_si512();
        auto stripes = zero, bodies = zero, highs = zero;
        for (; n; --n, p += period)
            detail::each<vectors>([&](auto v) __attribute__((always_inline)) {
                const auto x = _mm512_loadu_si512(p + v * 64);
                constexpr int a = role[2 * v], b = role[2 * v + 1];
                constexpr __mmask8 stripe_lanes = (a >= 0 ? 0x0f : 0) | (b >= 0 ? 0xf0 : 0);
                if constexpr (stripe_lanes) {
                    static constexpr auto table = [] {
                        std::array<std::uint8_t, 128> t{};
                        for (unsigned h = 0; h < 2; ++h) {
                            const int s = h ? b : a;
                            if (s >= 0)
                                for (unsigned q = 0; q < 2; ++q)
                                    for (unsigned e = 0; e < 16; ++e) {
                                        t[h * 32 + q * 16 + e] = nibbles[s][e];
                                        t[64 + h * 32 + q * 16 + e] = nibbles[s][16 + e];
                                    }
                        }
                        return t;
                    }();
                    const auto lo = _mm512_shuffle_epi8(_mm512_loadu_si512(table.data()), _mm512_and_si512(x, nibble));
                    const auto hi = _mm512_shuffle_epi8(_mm512_loadu_si512(table.data() + 64),
                                                        _mm512_and_si512(_mm512_srli_epi16(x, 4), nibble));
                    stripes = _mm512_mask_add_epi64(stripes, stripe_lanes, stripes, _mm512_sad_epu8(_mm512_add_epi8(lo, hi), zero));
                }
                constexpr __mmask8 body_lanes = static_cast<__mmask8>(~stripe_lanes);
                if constexpr (body_lanes != 0) {
                    if constexpr (B == 1)
                        bodies = _mm512_mask_add_epi64(bodies, body_lanes, bodies, _mm512_sad_epu8(x, zero));
                    else {
                        const auto even = _mm512_set1_epi16(0xff);
                        bodies = _mm512_mask_add_epi64(bodies, body_lanes, bodies, _mm512_sad_epu8(_mm512_and_si512(x, even), zero));
                        highs = _mm512_mask_add_epi64(highs, body_lanes, highs, _mm512_sad_epu8(_mm512_srli_epi16(x, 8), zero));
                    }
                }
            });
        planes = _mm512_reduce_add_epi64(stripes);
        low = _mm512_reduce_add_epi64(bodies);
        high = _mm512_reduce_add_epi64(highs);
#endif
        return planes + ((low + (high << 8)) << R);
    }
};
} // namespace ikea::seriespack::detail
