#pragma once
#include <ikea/comparison.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif

namespace ikea::detail::filter {
#if defined(__aarch64__)
/// Full-lane predicates stay native for a fused consumer.
template <unsigned Bytes, comparison Op>
[[gnu::always_inline]] inline uint8x16_t lanes(uint8x16_t x, std::uint64_t rhs) {
    if constexpr (Bytes == 1) {
        const auto a = x;
        const auto b = vdupq_n_u8(rhs);
        if constexpr (Op == comparison::equal)
            return vceqq_u8(a, b);
        if constexpr (Op == comparison::less)
            return vcltq_u8(a, b);
        if constexpr (Op == comparison::less_equal)
            return vcleq_u8(a, b);
        if constexpr (Op == comparison::greater)
            return vcgtq_u8(a, b);
        if constexpr (Op == comparison::greater_equal)
            return vcgeq_u8(a, b);
    }
    if constexpr (Bytes == 2) {
        const auto a = vreinterpretq_u16_u8(x);
        const auto b = vdupq_n_u16(rhs);
        if constexpr (Op == comparison::equal)
            return vreinterpretq_u8_u16(vceqq_u16(a, b));
        if constexpr (Op == comparison::less)
            return vreinterpretq_u8_u16(vcltq_u16(a, b));
        if constexpr (Op == comparison::less_equal)
            return vreinterpretq_u8_u16(vcleq_u16(a, b));
        if constexpr (Op == comparison::greater)
            return vreinterpretq_u8_u16(vcgtq_u16(a, b));
        if constexpr (Op == comparison::greater_equal)
            return vreinterpretq_u8_u16(vcgeq_u16(a, b));
    }
    if constexpr (Bytes == 4) {
        const auto a = vreinterpretq_u32_u8(x);
        const auto b = vdupq_n_u32(rhs);
        if constexpr (Op == comparison::equal)
            return vreinterpretq_u8_u32(vceqq_u32(a, b));
        if constexpr (Op == comparison::less)
            return vreinterpretq_u8_u32(vcltq_u32(a, b));
        if constexpr (Op == comparison::less_equal)
            return vreinterpretq_u8_u32(vcleq_u32(a, b));
        if constexpr (Op == comparison::greater)
            return vreinterpretq_u8_u32(vcgtq_u32(a, b));
        if constexpr (Op == comparison::greater_equal)
            return vreinterpretq_u8_u32(vcgeq_u32(a, b));
    }
    if constexpr (Bytes == 8) {
        const auto a = vreinterpretq_u64_u8(x);
        const auto b = vdupq_n_u64(rhs);
        if constexpr (Op == comparison::equal)
            return vreinterpretq_u8_u64(vceqq_u64(a, b));
        if constexpr (Op == comparison::less)
            return vreinterpretq_u8_u64(vcltq_u64(a, b));
        if constexpr (Op == comparison::less_equal)
            return vreinterpretq_u8_u64(vcleq_u64(a, b));
        if constexpr (Op == comparison::greater)
            return vreinterpretq_u8_u64(vcgtq_u64(a, b));
        if constexpr (Op == comparison::greater_equal)
            return vreinterpretq_u8_u64(vcgeq_u64(a, b));
    }
}
/// One bit per byte. Each lane must contain 0 or 255.
[[gnu::always_inline]] inline unsigned byte_bits(uint8x16_t p) {
    const auto weights = vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL));
    const auto weighted = vandq_u8(p, weights);
    return vaddv_u8(vget_low_u8(weighted)) | (unsigned(vaddv_u8(vget_high_u8(weighted))) << 8);
}
template <unsigned Bytes, comparison Op>
[[gnu::always_inline]] inline unsigned bits(uint8x16_t x, std::uint64_t rhs) {
    const auto p = lanes<Bytes, Op>(x, rhs);
    if constexpr (Bytes == 1)
        return byte_bits(p);
    else if constexpr (Bytes == 2) {
        const auto packed = vmovn_u16(vreinterpretq_u16_u8(p));
        return vaddv_u8(vand_u8(packed, vreinterpret_u8_u64(vcreate_u64(0x8040201008040201ULL))));
    } else if constexpr (Bytes == 4) {
        const auto packed =
            vmovn_u16(vcombine_u16(vmovn_u32(vreinterpretq_u32_u8(p)), vdup_n_u16(0)));
        return vaddv_u8(vand_u8(packed, vreinterpret_u8_u64(vcreate_u64(0x08040201ULL))));
    } else {
        return (vgetq_lane_u64(vreinterpretq_u64_u8(p), 0) & 1) |
               (vgetq_lane_u64(vreinterpretq_u64_u8(p), 1) & 2);
    }
}
#elif defined(__AVX2__)
template <unsigned Bytes, comparison Op>
[[gnu::always_inline]] inline unsigned long long bits(__m128i x, std::uint64_t rhs) {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    if constexpr (Op == comparison::equal) {
        if constexpr (Bytes == 1)
            return _mm_cmp_epu8_mask(x, _mm_set1_epi8(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 2)
            return _mm_cmp_epu16_mask(x, _mm_set1_epi16(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 4)
            return _mm_cmp_epu32_mask(x, _mm_set1_epi32(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 8)
            return _mm_cmp_epu64_mask(x, _mm_set1_epi64x(rhs), _MM_CMPINT_EQ);
    } else if constexpr (Op == comparison::less) {
        if constexpr (Bytes == 1)
            return _mm_cmp_epu8_mask(x, _mm_set1_epi8(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 2)
            return _mm_cmp_epu16_mask(x, _mm_set1_epi16(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 4)
            return _mm_cmp_epu32_mask(x, _mm_set1_epi32(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 8)
            return _mm_cmp_epu64_mask(x, _mm_set1_epi64x(rhs), _MM_CMPINT_LT);
    } else if constexpr (Op == comparison::less_equal) {
        if constexpr (Bytes == 1)
            return _mm_cmp_epu8_mask(x, _mm_set1_epi8(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 2)
            return _mm_cmp_epu16_mask(x, _mm_set1_epi16(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 4)
            return _mm_cmp_epu32_mask(x, _mm_set1_epi32(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 8)
            return _mm_cmp_epu64_mask(x, _mm_set1_epi64x(rhs), _MM_CMPINT_LE);
    } else if constexpr (Op == comparison::greater) {
        if constexpr (Bytes == 1)
            return _mm_cmp_epu8_mask(x, _mm_set1_epi8(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 2)
            return _mm_cmp_epu16_mask(x, _mm_set1_epi16(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 4)
            return _mm_cmp_epu32_mask(x, _mm_set1_epi32(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 8)
            return _mm_cmp_epu64_mask(x, _mm_set1_epi64x(rhs), _MM_CMPINT_NLE);
    } else if constexpr (Op == comparison::greater_equal) {
        if constexpr (Bytes == 1)
            return _mm_cmp_epu8_mask(x, _mm_set1_epi8(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 2)
            return _mm_cmp_epu16_mask(x, _mm_set1_epi16(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 4)
            return _mm_cmp_epu32_mask(x, _mm_set1_epi32(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 8)
            return _mm_cmp_epu64_mask(x, _mm_set1_epi64x(rhs), _MM_CMPINT_NLT);
    }
#else
    if constexpr (Bytes == 1) {
        const auto c = _mm_set1_epi8(rhs);
        __m128i p;
        if constexpr (Op == comparison::equal)
            p = _mm_cmpeq_epi8(x, c);
        else {
            const auto sign = _mm_set1_epi8(std::uint64_t{1} << 7);
            const auto a = _mm_xor_si128(x, sign), b = _mm_xor_si128(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm_cmpgt_epi8(b, a);
            else
                p = _mm_cmpgt_epi8(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm_xor_si128(p, _mm_set1_epi8(-1));
        }
        return unsigned(_mm_movemask_epi8(p));
    }
    if constexpr (Bytes == 2) {
        const auto c = _mm_set1_epi16(rhs);
        __m128i p;
        if constexpr (Op == comparison::equal)
            p = _mm_cmpeq_epi16(x, c);
        else {
            const auto sign = _mm_set1_epi16(std::uint64_t{1} << 15);
            const auto a = _mm_xor_si128(x, sign), b = _mm_xor_si128(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm_cmpgt_epi16(b, a);
            else
                p = _mm_cmpgt_epi16(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm_xor_si128(p, _mm_set1_epi8(-1));
        }
        return unsigned(_mm_movemask_epi8(_mm_packs_epi16(p, _mm_setzero_si128()))) & 255;
    }
    if constexpr (Bytes == 4) {
        const auto c = _mm_set1_epi32(rhs);
        __m128i p;
        if constexpr (Op == comparison::equal)
            p = _mm_cmpeq_epi32(x, c);
        else {
            const auto sign = _mm_set1_epi32(std::uint64_t{1} << 31);
            const auto a = _mm_xor_si128(x, sign), b = _mm_xor_si128(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm_cmpgt_epi32(b, a);
            else
                p = _mm_cmpgt_epi32(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm_xor_si128(p, _mm_set1_epi8(-1));
        }
        return unsigned(_mm_movemask_ps(_mm_castsi128_ps(p)));
    }
    if constexpr (Bytes == 8) {
        const auto c = _mm_set1_epi64x(rhs);
        __m128i p;
        if constexpr (Op == comparison::equal)
            p = _mm_cmpeq_epi64(x, c);
        else {
            const auto sign = _mm_set1_epi64x(std::uint64_t{1} << 63);
            const auto a = _mm_xor_si128(x, sign), b = _mm_xor_si128(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm_cmpgt_epi64(b, a);
            else
                p = _mm_cmpgt_epi64(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm_xor_si128(p, _mm_set1_epi8(-1));
        }
        return unsigned(_mm_movemask_pd(_mm_castsi128_pd(p)));
    }
#endif
}
template <unsigned Bytes, comparison Op>
[[gnu::always_inline]] inline unsigned long long bits(__m256i x, std::uint64_t rhs) {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    if constexpr (Op == comparison::equal) {
        if constexpr (Bytes == 1)
            return _mm256_cmp_epu8_mask(x, _mm256_set1_epi8(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 2)
            return _mm256_cmp_epu16_mask(x, _mm256_set1_epi16(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 4)
            return _mm256_cmp_epu32_mask(x, _mm256_set1_epi32(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 8)
            return _mm256_cmp_epu64_mask(x, _mm256_set1_epi64x(rhs), _MM_CMPINT_EQ);
    } else if constexpr (Op == comparison::less) {
        if constexpr (Bytes == 1)
            return _mm256_cmp_epu8_mask(x, _mm256_set1_epi8(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 2)
            return _mm256_cmp_epu16_mask(x, _mm256_set1_epi16(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 4)
            return _mm256_cmp_epu32_mask(x, _mm256_set1_epi32(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 8)
            return _mm256_cmp_epu64_mask(x, _mm256_set1_epi64x(rhs), _MM_CMPINT_LT);
    } else if constexpr (Op == comparison::less_equal) {
        if constexpr (Bytes == 1)
            return _mm256_cmp_epu8_mask(x, _mm256_set1_epi8(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 2)
            return _mm256_cmp_epu16_mask(x, _mm256_set1_epi16(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 4)
            return _mm256_cmp_epu32_mask(x, _mm256_set1_epi32(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 8)
            return _mm256_cmp_epu64_mask(x, _mm256_set1_epi64x(rhs), _MM_CMPINT_LE);
    } else if constexpr (Op == comparison::greater) {
        if constexpr (Bytes == 1)
            return _mm256_cmp_epu8_mask(x, _mm256_set1_epi8(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 2)
            return _mm256_cmp_epu16_mask(x, _mm256_set1_epi16(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 4)
            return _mm256_cmp_epu32_mask(x, _mm256_set1_epi32(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 8)
            return _mm256_cmp_epu64_mask(x, _mm256_set1_epi64x(rhs), _MM_CMPINT_NLE);
    } else if constexpr (Op == comparison::greater_equal) {
        if constexpr (Bytes == 1)
            return _mm256_cmp_epu8_mask(x, _mm256_set1_epi8(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 2)
            return _mm256_cmp_epu16_mask(x, _mm256_set1_epi16(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 4)
            return _mm256_cmp_epu32_mask(x, _mm256_set1_epi32(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 8)
            return _mm256_cmp_epu64_mask(x, _mm256_set1_epi64x(rhs), _MM_CMPINT_NLT);
    }
#else
    if constexpr (Bytes == 1) {
        const auto c = _mm256_set1_epi8(rhs);
        __m256i p;
        if constexpr (Op == comparison::equal)
            p = _mm256_cmpeq_epi8(x, c);
        else {
            const auto sign = _mm256_set1_epi8(std::uint64_t{1} << 7);
            const auto a = _mm256_xor_si256(x, sign), b = _mm256_xor_si256(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm256_cmpgt_epi8(b, a);
            else
                p = _mm256_cmpgt_epi8(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm256_xor_si256(p, _mm256_set1_epi8(-1));
        }
        return unsigned(_mm256_movemask_epi8(p));
    }
    if constexpr (Bytes == 2) {
        const auto c = _mm256_set1_epi16(rhs);
        __m256i p;
        if constexpr (Op == comparison::equal)
            p = _mm256_cmpeq_epi16(x, c);
        else {
            const auto sign = _mm256_set1_epi16(std::uint64_t{1} << 15);
            const auto a = _mm256_xor_si256(x, sign), b = _mm256_xor_si256(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm256_cmpgt_epi16(b, a);
            else
                p = _mm256_cmpgt_epi16(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm256_xor_si256(p, _mm256_set1_epi8(-1));
        }
        return unsigned(_mm_movemask_epi8(
            _mm_packs_epi16(_mm256_castsi256_si128(p), _mm256_extracti128_si256(p, 1))));
    }
    if constexpr (Bytes == 4) {
        const auto c = _mm256_set1_epi32(rhs);
        __m256i p;
        if constexpr (Op == comparison::equal)
            p = _mm256_cmpeq_epi32(x, c);
        else {
            const auto sign = _mm256_set1_epi32(std::uint64_t{1} << 31);
            const auto a = _mm256_xor_si256(x, sign), b = _mm256_xor_si256(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm256_cmpgt_epi32(b, a);
            else
                p = _mm256_cmpgt_epi32(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm256_xor_si256(p, _mm256_set1_epi8(-1));
        }
        return unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(p)));
    }
    if constexpr (Bytes == 8) {
        const auto c = _mm256_set1_epi64x(rhs);
        __m256i p;
        if constexpr (Op == comparison::equal)
            p = _mm256_cmpeq_epi64(x, c);
        else {
            const auto sign = _mm256_set1_epi64x(std::uint64_t{1} << 63);
            const auto a = _mm256_xor_si256(x, sign), b = _mm256_xor_si256(c, sign);
            if constexpr (Op == comparison::less || Op == comparison::greater_equal)
                p = _mm256_cmpgt_epi64(b, a);
            else
                p = _mm256_cmpgt_epi64(a, b);
            if constexpr (Op == comparison::less_equal || Op == comparison::greater_equal)
                p = _mm256_xor_si256(p, _mm256_set1_epi8(-1));
        }
        return unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(p)));
    }
#endif
}
#if defined(__AVX512BW__)
template <unsigned Bytes, comparison Op>
[[gnu::always_inline]] inline unsigned long long bits(__m512i x, std::uint64_t rhs) {
    if constexpr (Op == comparison::equal) {
        if constexpr (Bytes == 1)
            return _mm512_cmp_epu8_mask(x, _mm512_set1_epi8(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 2)
            return _mm512_cmp_epu16_mask(x, _mm512_set1_epi16(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 4)
            return _mm512_cmp_epu32_mask(x, _mm512_set1_epi32(rhs), _MM_CMPINT_EQ);
        if constexpr (Bytes == 8)
            return _mm512_cmp_epu64_mask(x, _mm512_set1_epi64(rhs), _MM_CMPINT_EQ);
    } else if constexpr (Op == comparison::less) {
        if constexpr (Bytes == 1)
            return _mm512_cmp_epu8_mask(x, _mm512_set1_epi8(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 2)
            return _mm512_cmp_epu16_mask(x, _mm512_set1_epi16(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 4)
            return _mm512_cmp_epu32_mask(x, _mm512_set1_epi32(rhs), _MM_CMPINT_LT);
        if constexpr (Bytes == 8)
            return _mm512_cmp_epu64_mask(x, _mm512_set1_epi64(rhs), _MM_CMPINT_LT);
    } else if constexpr (Op == comparison::less_equal) {
        if constexpr (Bytes == 1)
            return _mm512_cmp_epu8_mask(x, _mm512_set1_epi8(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 2)
            return _mm512_cmp_epu16_mask(x, _mm512_set1_epi16(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 4)
            return _mm512_cmp_epu32_mask(x, _mm512_set1_epi32(rhs), _MM_CMPINT_LE);
        if constexpr (Bytes == 8)
            return _mm512_cmp_epu64_mask(x, _mm512_set1_epi64(rhs), _MM_CMPINT_LE);
    } else if constexpr (Op == comparison::greater) {
        if constexpr (Bytes == 1)
            return _mm512_cmp_epu8_mask(x, _mm512_set1_epi8(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 2)
            return _mm512_cmp_epu16_mask(x, _mm512_set1_epi16(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 4)
            return _mm512_cmp_epu32_mask(x, _mm512_set1_epi32(rhs), _MM_CMPINT_NLE);
        if constexpr (Bytes == 8)
            return _mm512_cmp_epu64_mask(x, _mm512_set1_epi64(rhs), _MM_CMPINT_NLE);
    } else if constexpr (Op == comparison::greater_equal) {
        if constexpr (Bytes == 1)
            return _mm512_cmp_epu8_mask(x, _mm512_set1_epi8(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 2)
            return _mm512_cmp_epu16_mask(x, _mm512_set1_epi16(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 4)
            return _mm512_cmp_epu32_mask(x, _mm512_set1_epi32(rhs), _MM_CMPINT_NLT);
        if constexpr (Bytes == 8)
            return _mm512_cmp_epu64_mask(x, _mm512_set1_epi64(rhs), _MM_CMPINT_NLT);
    }
}
#endif
#endif
} // namespace ikea::detail::filter
