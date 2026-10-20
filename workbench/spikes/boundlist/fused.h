#pragma once
// Independent controls: no Ikea/candidate includes, views, routing, predicates
// or prefix bodies. Inputs are raw admitted arrays with exact lengths.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace boundlist_fused {
inline unsigned first_live(const std::uint64_t* live, unsigned first, unsigned n) {
    if (!live) return first;
    for (unsigned r = first; r < n; ++r) if (live[r / 64] >> (r % 64) & 1) return r;
    return n;
}
template <bool Inclusive, class T> [[gnu::always_inline]] unsigned flat(const T* end, unsigned n, T point,
                                                const std::uint64_t* live = nullptr) {
    unsigned i = 0;
    if constexpr (std::is_unsigned_v<T> && sizeof(T) == 2) {
#if defined(__AVX2__)
        for (; i + 16 <= n; i += 16) {
            const auto v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(end + i));
            const auto q = _mm256_set1_epi16(point);
            unsigned mask;
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            mask = _mm256_cmp_epu16_mask(v, q, Inclusive ? _MM_CMPINT_NLT : _MM_CMPINT_NLE);
#else
            const auto bias = _mm256_set1_epi16(INT16_MIN);
            auto p = _mm256_cmpgt_epi16(_mm256_xor_si256(v, bias), _mm256_xor_si256(q, bias));
            if constexpr (Inclusive) p = _mm256_or_si256(p, _mm256_cmpeq_epi16(v, q));
            const auto packed = _mm_packs_epi16(_mm256_castsi256_si128(p), _mm256_extracti128_si256(p, 1));
            mask = unsigned(_mm_movemask_epi8(packed));
#endif
            if (live) mask &= unsigned(live[i / 64] >> (i % 64));
            if (mask) return i + std::countr_zero(mask);
        }
#elif defined(__aarch64__)
        for (; i + 8 <= n; i += 8) {
            const auto v = vld1q_u16(end + i), q = vdupq_n_u16(point);
            const auto p = Inclusive ? vcgeq_u16(v, q) : vcgtq_u16(v, q);
            unsigned mask = vaddvq_u16(vandq_u16(p, uint16x8_t{1, 2, 4, 8, 16, 32, 64, 128}));
            if (live) mask &= unsigned(live[i / 64] >> (i % 64));
            if (mask) return i + std::countr_zero(mask);
        }
#endif
    } else if constexpr (std::is_unsigned_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)) {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        constexpr unsigned lanes = 64 / sizeof(T);
        for (; i + lanes <= n; i += lanes) {
            const auto v = _mm512_loadu_si512(end + i);
            unsigned mask;
            if constexpr (sizeof(T) == 4)
                mask = _mm512_cmp_epu32_mask(v, _mm512_set1_epi32(point), Inclusive ? _MM_CMPINT_NLT : _MM_CMPINT_NLE);
            else mask = _mm512_cmp_epu64_mask(v, _mm512_set1_epi64(point), Inclusive ? _MM_CMPINT_NLT : _MM_CMPINT_NLE);
            if (live) mask &= unsigned(live[i / 64] >> (i % 64));
            if (mask) return i + std::countr_zero(mask);
        }
#elif defined(__AVX2__)
        constexpr unsigned lanes = 32 / sizeof(T);
        for (; i + lanes <= n; i += lanes) {
            const auto v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(end + i));
            __m256i pred;
            if constexpr (sizeof(T) == 4) {
                const auto q = _mm256_set1_epi32(point), bias = _mm256_set1_epi32(INT32_MIN);
                pred = _mm256_cmpgt_epi32(_mm256_xor_si256(v, bias), _mm256_xor_si256(q, bias));
                if constexpr (Inclusive) pred = _mm256_or_si256(pred, _mm256_cmpeq_epi32(v, q));
            } else {
                const auto q = _mm256_set1_epi64x(point), bias = _mm256_set1_epi64x(INT64_MIN);
                pred = _mm256_cmpgt_epi64(_mm256_xor_si256(v, bias), _mm256_xor_si256(q, bias));
                if constexpr (Inclusive) pred = _mm256_or_si256(pred, _mm256_cmpeq_epi64(v, q));
            }
            unsigned mask;
            if constexpr (sizeof(T) == 4) mask = _mm256_movemask_ps(_mm256_castsi256_ps(pred));
            else mask = _mm256_movemask_pd(_mm256_castsi256_pd(pred));
            if (live) mask &= unsigned(live[i / 64] >> (i % 64));
            if (mask) return i + std::countr_zero(mask);
        }
#elif defined(__aarch64__)
        constexpr unsigned lanes = 16 / sizeof(T);
        for (; i + lanes <= n; i += lanes) {
            unsigned mask;
            if constexpr (sizeof(T) == 4) {
                const auto v = vld1q_u32(end + i), q = vdupq_n_u32(point);
                const auto p = Inclusive ? vcgeq_u32(v, q) : vcgtq_u32(v, q);
                mask = vaddvq_u32(vandq_u32(p, uint32x4_t{1, 2, 4, 8}));
            } else {
                const auto v = vld1q_u64(end + i), q = vdupq_n_u64(point);
                const auto p = Inclusive ? vcgeq_u64(v, q) : vcgtq_u64(v, q);
                mask = (vgetq_lane_u64(p, 0) & 1) | (vgetq_lane_u64(p, 1) & 2);
            }
            if (live) mask &= unsigned(live[i / 64] >> (i % 64));
            if (mask) return i + std::countr_zero(mask);
        }
#endif
    }
    for (; i < n; ++i)
        if ((Inclusive ? !(end[i] < point) : point < end[i]) &&
            (!live || (live[i / 64] >> (i % 64) & 1))) return i;
    return n;
}

template <bool Inclusive, bool Starts>
[[gnu::always_inline]] unsigned byte_owner(const std::uint8_t* w, unsigned q, unsigned active) {
    if constexpr (Inclusive) if (!q) return active ? std::countr_zero(active) : 16;
#if defined(__AVX2__)
    const auto values = _mm256_cvtepu8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(w)));
    auto x = _mm256_add_epi16(values, _mm256_slli_si256(values, 2));
    x = _mm256_add_epi16(x, _mm256_slli_si256(x, 4));
    x = _mm256_add_epi16(x, _mm256_slli_si256(x, 8));
    const auto carry = _mm_set1_epi16(_mm_extract_epi16(_mm256_castsi256_si128(x), 7));
    x = _mm256_add_epi16(x, _mm256_inserti128_si256(_mm256_setzero_si256(), carry, 1));
    if constexpr (Starts) x = _mm256_sub_epi16(x, values);
    const auto found = _mm256_cmpgt_epi16(x, _mm256_set1_epi16(q - unsigned(Inclusive)));
    if (active == 65535) return std::countr_zero(unsigned(_mm256_movemask_epi8(found))) / 2;
    const auto bytes = _mm_packs_epi16(_mm256_castsi256_si128(found), _mm256_extracti128_si256(found, 1));
    const auto mask = unsigned(_mm_movemask_epi8(bytes)) & active;
    return mask ? std::countr_zero(mask) : 16;
#elif defined(__aarch64__)
    const auto bytes = vld1q_u8(w);
    const auto values0 = vmovl_u8(vget_low_u8(bytes)), values1 = vmovl_u8(vget_high_u8(bytes));
    const auto zero = vdupq_n_u16(0);
    auto a = vaddq_u16(values0, vextq_u16(zero, values0, 7));
    auto b = vaddq_u16(values1, vextq_u16(zero, values1, 7));
    a = vaddq_u16(a, vextq_u16(zero, a, 6)); b = vaddq_u16(b, vextq_u16(zero, b, 6));
    a = vaddq_u16(a, vextq_u16(zero, a, 4)); b = vaddq_u16(b, vextq_u16(zero, b, 4));
    b = vaddq_u16(b, vdupq_n_u16(vgetq_lane_u16(a, 7)));
    if constexpr (Starts) { a = vsubq_u16(a, values0); b = vsubq_u16(b, values1); }
    const auto bound = vdupq_n_u16(q);
    if (active == 65535) {
        const auto x = Inclusive ? vcltq_u16(a, bound) : vcleq_u16(a, bound);
        const auto y = Inclusive ? vcltq_u16(b, bound) : vcleq_u16(b, bound);
        return vaddvq_u16(vaddq_u16(vshrq_n_u16(x, 15), vshrq_n_u16(y, 15)));
    }
    const auto x = Inclusive ? vcgeq_u16(a, bound) : vcgtq_u16(a, bound);
    const auto y = Inclusive ? vcgeq_u16(b, bound) : vcgtq_u16(b, bound);
    const auto weights = vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL));
    const auto low = vaddv_u8(vand_u8(vmovn_u16(x), weights));
    const auto high = vaddv_u8(vand_u8(vmovn_u16(y), weights));
    const auto mask = (unsigned(low) | (unsigned(high) << 8)) & active;
    return mask ? std::countr_zero(mask) : 16;
#else
    unsigned end = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if constexpr (!Starts) end += w[i];
        if ((active >> i & 1) && (Inclusive ? end >= q : end > q)) return i;
        if constexpr (Starts) end += w[i];
    }
    return 16;
#endif
}

template <bool Inclusive, bool Starts = false, class W, class O>
[[gnu::always_inline]] unsigned width(const W* widths, const O* starts, unsigned stride, unsigned n, O extent,
               O point, const std::uint64_t* live = nullptr) {
    const auto groups = (n + 15) / 16;
    if (!groups) return n;
    // Search actual group starts including start0, so the native load is bounded
    // without a materialized terminal sentinel. Convert to first group end.
    const auto next = flat<Inclusive>(starts, groups, point);
    auto group = next ? next - 1 : 0;
    if (next == groups && !(Inclusive ? !(extent < point) : point < extent)) return n;
    for (; group < groups; ++group) {
        const auto* w = widths + group * stride;
        const auto first = group * 16, take = std::min(16u, n - first);
        const auto active = live ? unsigned(live[first / 64] >> (first % 64)) : 65535u;
        if (!(active & (take == 16 ? 65535u : ((1u << take) - 1)))) continue;
        if (point < starts[group]) return first_live(live, first, first + take);
        const O q = O(point - starts[group]);
        if (q > UINT32_MAX) continue;
        if constexpr (sizeof(W) == 1) {
            if (q > 4080) continue;
            const auto mask = active & (take == 16 ? 65535u : ((1u << take) - 1));
            const auto rank = byte_owner<Inclusive, Starts>(w, q, mask);
            if (rank < take) return first + rank;
            continue;
        }
        unsigned carry = 0;
#if defined(__AVX2__)
        // Two YMM prefixes deliberately provide a separately fused alternative
        // to the candidate's one-ZMM prefix in AVX-512 builds.
        for (unsigned i = 0; i < 16; i += 8) {
            __m256i x;
            if constexpr (sizeof(W) == 1)
                x = _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(w + i)));
            else x = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(w + i)));
            const auto values = x;
            x = _mm256_add_epi32(x, _mm256_slli_si256(x, 4));
            x = _mm256_add_epi32(x, _mm256_slli_si256(x, 8));
            const auto low = _mm_extract_epi32(_mm256_castsi256_si128(x), 3);
            x = _mm256_add_epi32(x, _mm256_setr_epi32(carry, carry, carry, carry,
                                                    carry + low, carry + low, carry + low, carry + low));
            carry = _mm256_extract_epi32(x, 7);
            if constexpr (Starts) x = _mm256_sub_epi32(x, values);
            const auto y = _mm256_set1_epi32(q), bias = _mm256_set1_epi32(INT32_MIN);
            auto p = _mm256_cmpgt_epi32(_mm256_xor_si256(x, bias), _mm256_xor_si256(y, bias));
            if constexpr (Inclusive) p = _mm256_or_si256(p, _mm256_cmpeq_epi32(x, y));
            auto mask = unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(p)));
            mask &= (active >> i) & 255u;
            if (i + 8 > take) mask &= take > i ? (1u << (take - i)) - 1 : 0;
            if (mask) return first + i + std::countr_zero(mask);
        }
#elif defined(__aarch64__)
        for (unsigned i = 0; i < 16; i += 4) {
            uint32x4_t x;
            if constexpr (sizeof(W) == 1) {
                std::uint32_t raw;
                std::memcpy(&raw, w + i, sizeof(raw));
                x = vmovl_u16(vget_low_u16(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(raw)))));
            } else {
                std::uint64_t raw;
                std::memcpy(&raw, w + i, sizeof(raw));
                x = vmovl_u16(vreinterpret_u16_u64(vcreate_u64(raw)));
            }
            const auto values = x;
            const auto zero = vdupq_n_u32(0);
            x = vaddq_u32(x, vextq_u32(zero, x, 3));
            x = vaddq_u32(x, vextq_u32(zero, x, 2));
            x = vaddq_u32(x, vdupq_n_u32(carry));
            carry = vgetq_lane_u32(x, 3);
            if constexpr (Starts) x = vsubq_u32(x, values);
            const auto p = Inclusive ? vcgeq_u32(x, vdupq_n_u32(q)) : vcgtq_u32(x, vdupq_n_u32(q));
            auto mask = vaddvq_u32(vandq_u32(p, uint32x4_t{1, 2, 4, 8}));
            mask &= (active >> i) & 15u;
            if (i + 4 > take) mask &= take > i ? (1u << (take - i)) - 1 : 0;
            if (mask) return first + i + std::countr_zero(mask);
        }
#else
        O end = starts[group];
        for (unsigned i = 0; i < take; ++i) {
            if constexpr (!Starts) end += w[i];
            if ((active >> i & 1) && (Inclusive ? !(end < point) : point < end)) return first + i;
            if constexpr (Starts) end += w[i];
        }
#endif
    }
    return n;
}
} // namespace boundlist_fused
