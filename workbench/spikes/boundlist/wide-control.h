#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace boundlist_wide_control {
// Independent lexicographic 128-bit control. T has consecutive high/low uint64
// words. No Ikea decoder, comparator, native values or mask helpers are called.
template <bool Inclusive, class T>
[[gnu::always_inline]] unsigned flat(const T* p, unsigned n, const T& q, const std::uint64_t* live = nullptr) {
    static_assert(sizeof(T) == 16);
    unsigned i = 0;
#if defined(__AVX512F__)
    for (; i + 4 <= n; i += 4) {
        const auto x = _mm512_loadu_si512(p + i);
        const auto y = _mm512_setr_epi64(q.high, q.low, q.high, q.low, q.high, q.low, q.high, q.low);
        const unsigned gt = _mm512_cmp_epu64_mask(x, y, _MM_CMPINT_GT), eq = _mm512_cmpeq_epi64_mask(x, y);
        const auto low = Inclusive ? gt | eq : gt;
        auto mask = (gt | (eq & (low >> 1))) & 0x55u;
        mask = (mask | (mask >> 1)) & 0x33u;
        mask = (mask | (mask >> 2)) & 15u;
        if (live) mask &= live[i / 64] >> (i % 64);
        if (mask) return i + std::countr_zero(mask);
    }
#endif
#if defined(__AVX2__)
    for (; i + 2 <= n; i += 2) {
        const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
        const auto y = _mm256_setr_epi64x(q.high, q.low, q.high, q.low), bias = _mm256_set1_epi64x(INT64_MIN);
        const auto greater = _mm256_cmpgt_epi64(_mm256_xor_si256(x, bias), _mm256_xor_si256(y, bias));
        const unsigned gt = _mm256_movemask_pd(_mm256_castsi256_pd(greater));
        const unsigned eq = _mm256_movemask_pd(_mm256_castsi256_pd(_mm256_cmpeq_epi64(x, y)));
        const auto low = Inclusive ? gt | eq : gt;
        auto mask = (gt | (eq & (low >> 1))) & 5u;
        mask = (mask | (mask >> 1)) & 3u;
        if (live) mask &= live[i / 64] >> (i % 64);
        if (mask) return i + std::countr_zero(mask);
    }
#elif defined(__aarch64__)
    for (; i + 2 <= n; i += 2) {
        const auto x = vld2q_u64(reinterpret_cast<const std::uint64_t*>(p + i));
        const auto a = vdupq_n_u64(q.high), b = vdupq_n_u64(q.low);
        const auto low = Inclusive ? vcgeq_u64(x.val[1], b) : vcgtq_u64(x.val[1], b);
        const auto keep = vorrq_u64(vcgtq_u64(x.val[0], a), vandq_u64(vceqq_u64(x.val[0], a), low));
        unsigned mask = (vgetq_lane_u64(keep, 0) & 1) | (vgetq_lane_u64(keep, 1) & 2);
        if (live) mask &= live[i / 64] >> (i % 64);
        if (mask) return i + std::countr_zero(mask);
    }
#endif
    for (; i < n; ++i) if ((!live || (live[i / 64] >> (i % 64) & 1)) &&
        (p[i].high > q.high || (p[i].high == q.high && (Inclusive ? p[i].low >= q.low : p[i].low > q.low)))) return i;
    return n;
}
} // namespace boundlist_wide_control
