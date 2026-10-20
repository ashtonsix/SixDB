#pragma once
// Independently fused prefix control; no Ikea/candidate kernels or routing.
#include "fused.h"
namespace portfolio_fused {
[[gnu::always_inline]] unsigned full_prefix_owner(const std::uint16_t *p, unsigned point,
                                                  unsigned sum_bound) {
    if (sum_bound <= 65535) {
#if defined(__AVX2__)
        auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
        x = _mm256_add_epi16(x, _mm256_slli_si256(x, 2));
        x = _mm256_add_epi16(x, _mm256_slli_si256(x, 4));
        x = _mm256_add_epi16(x, _mm256_slli_si256(x, 8));
        const auto carry = _mm_set1_epi16(_mm_extract_epi16(_mm256_castsi256_si128(x), 7));
        x = _mm256_add_epi16(x, _mm256_inserti128_si256(_mm256_setzero_si256(), carry, 1));
        const auto sign = _mm256_set1_epi16(INT16_MIN);
        const auto gt = _mm256_cmpgt_epi16(_mm256_xor_si256(x, sign),
                                           _mm256_xor_si256(_mm256_set1_epi16(point), sign));
        return std::countr_zero(unsigned(_mm256_movemask_epi8(gt))) / 2;
#elif defined(__aarch64__)
        const auto zero = vdupq_n_u16(0);
        auto lo = vld1q_u16(p), hi = vld1q_u16(p + 8);
        lo = vaddq_u16(lo, vextq_u16(zero, lo, 7));
        hi = vaddq_u16(hi, vextq_u16(zero, hi, 7));
        lo = vaddq_u16(lo, vextq_u16(zero, lo, 6));
        hi = vaddq_u16(hi, vextq_u16(zero, hi, 6));
        lo = vaddq_u16(lo, vextq_u16(zero, lo, 4));
        hi = vaddq_u16(hi, vextq_u16(zero, hi, 4));
        hi = vaddq_u16(hi, vdupq_n_u16(vgetq_lane_u16(lo, 7)));
        const auto q = vdupq_n_u16(point);
        return vaddvq_u16(
            vaddq_u16(vshrq_n_u16(vcleq_u16(lo, q), 15), vshrq_n_u16(vcleq_u16(hi, q), 15)));
#endif
    }
#if defined(__AVX2__)
    unsigned carry = 0, mask = 0;
    for (unsigned i = 0; i < 16; i += 8) {
        auto x = _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(p + i)));
        x = _mm256_add_epi32(x, _mm256_slli_si256(x, 4));
        x = _mm256_add_epi32(x, _mm256_slli_si256(x, 8));
        const auto low = _mm_extract_epi32(_mm256_castsi256_si128(x), 3);
        x = _mm256_add_epi32(x, _mm256_setr_epi32(carry, carry, carry, carry, carry + low,
                                                  carry + low, carry + low, carry + low));
        carry = _mm256_extract_epi32(x, 7);
        mask |= unsigned(_mm256_movemask_ps(
                    _mm256_castsi256_ps(_mm256_cmpgt_epi32(x, _mm256_set1_epi32(point)))))
                << i;
    }
    return std::countr_zero(mask | 0x10000u);
#elif defined(__aarch64__)
    unsigned carry = 0, result = 0;
    for (unsigned i = 0; i < 16; i += 4) {
        auto x = vmovl_u16(vld1_u16(p + i));
        const auto zero = vdupq_n_u32(0);
        x = vaddq_u32(x, vextq_u32(zero, x, 3));
        x = vaddq_u32(x, vextq_u32(zero, x, 2));
        x = vaddq_u32(x, vdupq_n_u32(carry));
        carry = vgetq_lane_u32(x, 3);
        result += vaddvq_u32(vshrq_n_u32(vcleq_u32(x, vdupq_n_u32(point)), 31));
    }
    return result;
#else
    unsigned end = 0;
    for (unsigned r = 0; r < 16; ++r) {
        end += p[r];
        if (point < end)
            return r;
    }
    return 16;
#endif
}
} // namespace portfolio_fused
