#pragma once
// Independent full-scan control for ordered unsigned endpoints. Accumulate
// counts in vector registers, then cross to the integer side once per query.
// No Ikea predicates, source adapters, or search helpers. n <= 65535.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace boundlist_count_control {
template <bool Inclusive, class T>
[[gnu::always_inline]] unsigned scan(const T* data, unsigned n, T point);
// Same-list batch control: load each vector once, compare every query before
// moving on. One accumulator per query avoids the fixed256 consumer's four
// accumulators per lane exhausting NEON registers at Q8. This is separate from
// interleaving searches into distinct lists.
template <unsigned Q, class T>
[[gnu::always_inline]] std::array<unsigned, Q> batch(const T* data, unsigned n, const std::array<T, Q>& points) {
    __builtin_assume(n <= 65535);
    std::array<unsigned, Q> result{};
    unsigned i = 0;
#if defined(__aarch64__)
    constexpr unsigned lanes = 16 / sizeof(T);
    using vector = std::conditional_t<sizeof(T) == 2, uint16x8_t,
                   std::conditional_t<sizeof(T) == 4, uint32x4_t, uint64x2_t>>;
    std::array<vector, Q> queries, acc;
    for (unsigned k = 0; k < Q; ++k) {
        if constexpr (sizeof(T) == 2) { queries[k] = vdupq_n_u16(points[k]); acc[k] = vdupq_n_u16(0); }
        else if constexpr (sizeof(T) == 4) { queries[k] = vdupq_n_u32(points[k]); acc[k] = vdupq_n_u32(0); }
        else { queries[k] = vdupq_n_u64(points[k]); acc[k] = vdupq_n_u64(0); }
    }
    for (; i + lanes <= n; i += lanes) {
        const auto x = [&]() -> vector {
            if constexpr (sizeof(T) == 2) return vld1q_u16(data + i);
            else if constexpr (sizeof(T) == 4) return vld1q_u32(data + i);
            else return vld1q_u64(data + i);
        }();
#pragma clang loop unroll(full)
        for (unsigned k = 0; k < Q; ++k) {
            if constexpr (sizeof(T) == 2) acc[k] = vsubq_u16(acc[k], vcltq_u16(x, queries[k]));
            else if constexpr (sizeof(T) == 4) acc[k] = vsubq_u32(acc[k], vcltq_u32(x, queries[k]));
            else acc[k] = vsubq_u64(acc[k], vcltq_u64(x, queries[k]));
        }
    }
    for (unsigned k = 0; k < Q; ++k) {
        if constexpr (sizeof(T) == 2) result[k] = vaddvq_u16(acc[k]);
        else if constexpr (sizeof(T) == 4) result[k] = vaddvq_u32(acc[k]);
        else result[k] = unsigned(vaddvq_u64(acc[k]));
    }
#else
    // Existing independent native scan remains the x86 control; the ARM batch
    // specifically tests the observed load/register-pressure mechanism.
    for (unsigned k = 0; k < Q; ++k) result[k] = scan<true>(data, n, points[k]);
    return result;
#endif
    for (; i < n; ++i) for (unsigned k = 0; k < Q; ++k) result[k] += data[i] < points[k];
    return result;
}
template <bool Inclusive, class T>
[[gnu::always_inline]] unsigned scan(const T* data, unsigned n, T point) {
    static_assert(std::is_unsigned_v<T> && (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8));
    // This independently measured recipe admits at most65535 ranks; callers
    // validate capacity outside query loops.
    __builtin_assume(n <= 65535);
    unsigned i = 0, result = 0;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    constexpr unsigned lanes = 64 / sizeof(T);
    auto a = _mm512_setzero_si512(), b = a, c = a, d = a;
    const auto q = [&] {
        if constexpr (sizeof(T) == 2) return _mm512_set1_epi16(point);
        else if constexpr (sizeof(T) == 4) return _mm512_set1_epi32(point);
        else return _mm512_set1_epi64(point);
    }();
    auto add = [&](auto x, auto y) __attribute__((always_inline)) {
        if constexpr (sizeof(T) == 2) return _mm512_add_epi16(x, y);
        else if constexpr (sizeof(T) == 4) return _mm512_add_epi32(x, y);
        else return _mm512_add_epi64(x, y);
    };
    auto accumulate = [&](auto acc, const T* p) __attribute__((always_inline)) {
        const auto x = _mm512_loadu_si512(p);
        if constexpr (sizeof(T) == 2) return _mm512_sub_epi16(acc, _mm512_movm_epi16(
            _mm512_cmp_epu16_mask(x, q, Inclusive ? _MM_CMPINT_LT : _MM_CMPINT_LE)));
        else if constexpr (sizeof(T) == 4) return _mm512_sub_epi32(acc, _mm512_movm_epi32(
            _mm512_cmp_epu32_mask(x, q, Inclusive ? _MM_CMPINT_LT : _MM_CMPINT_LE)));
        else return _mm512_sub_epi64(acc, _mm512_movm_epi64(
            _mm512_cmp_epu64_mask(x, q, Inclusive ? _MM_CMPINT_LT : _MM_CMPINT_LE)));
    };
    for (; i + 4 * lanes <= n; i += 4 * lanes) {
        a = accumulate(a, data + i); b = accumulate(b, data + i + lanes);
        c = accumulate(c, data + i + 2 * lanes); d = accumulate(d, data + i + 3 * lanes);
    }
    a = add(add(a, b), add(c, d));
    for (; i + lanes <= n; i += lanes) a = accumulate(a, data + i);
    if constexpr (sizeof(T) == 2) result = _mm512_reduce_add_epi32(_mm512_madd_epi16(a, _mm512_set1_epi16(1)));
    else if constexpr (sizeof(T) == 4) result = _mm512_reduce_add_epi32(a);
    else result = unsigned(_mm512_reduce_add_epi64(a));
#elif defined(__AVX2__)
    constexpr unsigned lanes = 32 / sizeof(T);
    auto a = _mm256_setzero_si256(), b = a, c = a, d = a;
    const auto q = [&] {
        if constexpr (sizeof(T) == 2) return _mm256_set1_epi16(point ^ T(1u << 15));
        else if constexpr (sizeof(T) == 4) return _mm256_set1_epi32(point ^ T(1u << 31));
        else return _mm256_set1_epi64x(point ^ T(1ULL << 63));
    }();
    auto add = [&](auto x, auto y) __attribute__((always_inline)) {
        if constexpr (sizeof(T) == 2) return _mm256_add_epi16(x, y);
        else if constexpr (sizeof(T) == 4) return _mm256_add_epi32(x, y);
        else return _mm256_add_epi64(x, y);
    };
    auto accumulate = [&](auto acc, const T* p) __attribute__((always_inline)) {
        const auto v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        if constexpr (sizeof(T) == 2) {
            const auto x = _mm256_xor_si256(v, _mm256_set1_epi16(INT16_MIN));
            const auto pred = Inclusive ? _mm256_cmpgt_epi16(q, x) : _mm256_xor_si256(_mm256_cmpgt_epi16(x, q), _mm256_set1_epi32(-1));
            return _mm256_sub_epi16(acc, pred);
        } else if constexpr (sizeof(T) == 4) {
            const auto x = _mm256_xor_si256(v, _mm256_set1_epi32(INT32_MIN));
            const auto pred = Inclusive ? _mm256_cmpgt_epi32(q, x) : _mm256_xor_si256(_mm256_cmpgt_epi32(x, q), _mm256_set1_epi32(-1));
            return _mm256_sub_epi32(acc, pred);
        } else {
            const auto x = _mm256_xor_si256(v, _mm256_set1_epi64x(INT64_MIN));
            const auto pred = Inclusive ? _mm256_cmpgt_epi64(q, x) : _mm256_xor_si256(_mm256_cmpgt_epi64(x, q), _mm256_set1_epi32(-1));
            return _mm256_sub_epi64(acc, pred);
        }
    };
    for (; i + 4 * lanes <= n; i += 4 * lanes) {
        a = accumulate(a, data + i); b = accumulate(b, data + i + lanes);
        c = accumulate(c, data + i + 2 * lanes); d = accumulate(d, data + i + 3 * lanes);
    }
    a = add(add(a, b), add(c, d));
    for (; i + lanes <= n; i += lanes) a = accumulate(a, data + i);
    if constexpr (sizeof(T) == 2) a = _mm256_madd_epi16(a, _mm256_set1_epi16(1));
    auto sum = _mm_add_epi64(_mm256_castsi256_si128(a), _mm256_extracti128_si256(a, 1));
    if constexpr (sizeof(T) != 8) {
        // Add at 32-bit granularity; the preceding 64-bit addition cannot carry
        // from a low lane because all counts are <=65535.
        sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 8));
        sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 4));
        result = unsigned(_mm_cvtsi128_si32(sum));
    } else result = unsigned(_mm_cvtsi128_si64(sum) + _mm_extract_epi64(sum, 1));
#elif defined(__aarch64__)
    constexpr unsigned lanes = 16 / sizeof(T);
    using vector = std::conditional_t<sizeof(T) == 2, uint16x8_t,
                   std::conditional_t<sizeof(T) == 4, uint32x4_t, uint64x2_t>>;
    auto zero = [&]() -> vector {
        if constexpr (sizeof(T) == 2) return vdupq_n_u16(0);
        else if constexpr (sizeof(T) == 4) return vdupq_n_u32(0);
        else return vdupq_n_u64(0);
    };
    auto a = zero(), b = a, c = a, d = a;
    const auto q = [&]() -> vector {
        if constexpr (sizeof(T) == 2) return vdupq_n_u16(point);
        else if constexpr (sizeof(T) == 4) return vdupq_n_u32(point);
        else return vdupq_n_u64(point);
    }();
    auto add = [&](vector x, vector y) __attribute__((always_inline)) -> vector {
        if constexpr (sizeof(T) == 2) return vaddq_u16(x, y);
        else if constexpr (sizeof(T) == 4) return vaddq_u32(x, y);
        else return vaddq_u64(x, y);
    };
    auto accumulate = [&](vector acc, const T* p) __attribute__((always_inline)) -> vector {
        if constexpr (sizeof(T) == 2) {
            const auto x = vld1q_u16(p);
            return vsubq_u16(acc, Inclusive ? vcltq_u16(x, q) : vcleq_u16(x, q));
        } else if constexpr (sizeof(T) == 4) {
            const auto x = vld1q_u32(p);
            return vsubq_u32(acc, Inclusive ? vcltq_u32(x, q) : vcleq_u32(x, q));
        } else {
            const auto x = vld1q_u64(p);
            return vsubq_u64(acc, Inclusive ? vcltq_u64(x, q) : vcleq_u64(x, q));
        }
    };
    for (; i + 4 * lanes <= n; i += 4 * lanes) {
        a = accumulate(a, data + i); b = accumulate(b, data + i + lanes);
        c = accumulate(c, data + i + 2 * lanes); d = accumulate(d, data + i + 3 * lanes);
    }
    a = add(add(a, b), add(c, d));
    for (; i + lanes <= n; i += lanes) a = accumulate(a, data + i);
    if constexpr (sizeof(T) == 2) result = vaddvq_u16(a);
    else if constexpr (sizeof(T) == 4) result = vaddvq_u32(a);
    else result = unsigned(vaddvq_u64(a));
#endif
    for (; i < n; ++i) result += Inclusive ? data[i] < point : !(point < data[i]);
    return result;
}
} // namespace boundlist_count_control
