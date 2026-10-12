#pragma once
#include <bit>
#include <cstdint>
#include <ikea/streampack/detail/simd.h>
#include <type_traits>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace ikea::streampack::detail {
// Same widened prefix body as native::read16, with an explicit memory sink for
// paid dense selections. The fused author path keeps these vectors in registers.
template <class Width>
inline unsigned starts16_native(const Width* widths, unsigned* starts, unsigned carry) {
#if defined(__aarch64__) || defined(__AVX2__)
    for (unsigned i = 0; i < 16; i += native::lanes) {
        const auto values = native::load<Width>(reinterpret_cast<const byte*>(widths + i));
        const auto sums = native::prefix(values);
        native::store(starts + i, native::add(native::sub(sums, values), native::splat(carry)));
        carry += native::last(sums);
    }
#else
    for (unsigned i = 0; i < 16; ++i) {
        starts[i] = carry;
        carry += widths[i];
    }
#endif
    return carry;
}

inline unsigned owner16_native(const std::uint16_t* widths, unsigned offset) {
#if defined(__aarch64__) || defined(__AVX2__)
    unsigned carry = 0;
    for (unsigned i = 0; i < 16; i += native::lanes) {
        const auto sums =
            native::prefix(native::load<std::uint16_t>(reinterpret_cast<const byte*>(widths + i)));
        const auto mask = native::greater(native::add(sums, native::splat(carry)), offset);
        if (mask)
            return i + std::countr_zero(mask);
        carry += native::last(sums);
    }
#else
    unsigned end = 0;
    for (unsigned i = 0; i < 16; ++i) {
        end += widths[i];
        if (offset < end)
            return i;
    }
#endif
    return 16;
}

template <class Width> inline std::uint32_t prefix16_scalar(const Width* widths, unsigned count) {
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < 16; ++i)
        sum += i < count ? widths[i] : 0;
    return sum;
}
/// count is in [0,16]. Sixteen initialized widths are readable, even when count is smaller. Sum
/// widens before reduction; sixteen legal uint16 widths exceed uint16 range.
template <class Width> inline std::uint32_t prefix16_native(const Width* widths, unsigned count) {
    static_assert(std::is_same_v<Width, std::uint8_t> || std::is_same_v<Width, std::uint16_t>);
#if defined(__aarch64__)
    if constexpr (sizeof(Width) == 1) {
        const uint8x16_t lanes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        return vaddlvq_u8(vandq_u8(vld1q_u8(widths), vcltq_u8(lanes, vdupq_n_u8(count))));
    } else {
        const uint16x8_t lo{0, 1, 2, 3, 4, 5, 6, 7}, hi{8, 9, 10, 11, 12, 13, 14, 15};
        const auto bound = vdupq_n_u16(count);
        return vaddlvq_u16(vandq_u16(vld1q_u16(widths), vcltq_u16(lo, bound))) +
               vaddlvq_u16(vandq_u16(vld1q_u16(widths + 8), vcltq_u16(hi, bound)));
    }
#elif defined(__AVX2__)
    if constexpr (sizeof(Width) == 1) {
        const auto mask =
            _mm_cmpgt_epi8(_mm_set1_epi8(count),
                           _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
        const auto sums = _mm_sad_epu8(
            _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(widths)), mask),
            _mm_setzero_si128());
        return _mm_cvtsi128_si32(sums) + _mm_cvtsi128_si32(_mm_srli_si128(sums, 8));
    } else {
        const auto mask = _mm256_cmpgt_epi16(
            _mm256_set1_epi16(count),
            _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
        const auto selected =
            _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(widths)), mask);
        const auto sums =
            _mm256_add_epi32(_mm256_cvtepu16_epi32(_mm256_castsi256_si128(selected)),
                             _mm256_cvtepu16_epi32(_mm256_extracti128_si256(selected, 1)));
        auto four = _mm_add_epi32(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        four = _mm_add_epi32(four, _mm_srli_si128(four, 8));
        return _mm_cvtsi128_si32(_mm_add_epi32(four, _mm_srli_si128(four, 4)));
    }
#else
    return prefix16_scalar(widths, count);
#endif
}

/// First inclusive endpoint greater than offset, or 16 if none. Sixteen
/// initialized byte widths are readable; offset is <=4080. No persistent
/// decoded geometry is produced. Widen before adding: legal totals reach 4080.
inline unsigned owner16_native(const std::uint8_t* widths, unsigned offset) {
#if defined(__aarch64__)
    const auto bytes = vld1q_u8(widths);
    const auto zero = vdupq_n_u16(0);
    auto lo = vmovl_u8(vget_low_u8(bytes)), hi = vmovl_u8(vget_high_u8(bytes));
    lo = vaddq_u16(lo, vextq_u16(zero, lo, 7));
    hi = vaddq_u16(hi, vextq_u16(zero, hi, 7));
    lo = vaddq_u16(lo, vextq_u16(zero, lo, 6));
    hi = vaddq_u16(hi, vextq_u16(zero, hi, 6));
    lo = vaddq_u16(lo, vextq_u16(zero, lo, 4));
    hi = vaddq_u16(hi, vextq_u16(zero, hi, 4));
    hi = vaddq_u16(hi, vdupq_n_u16(vgetq_lane_u16(lo, 7)));
    const auto bound = vdupq_n_u16(offset);
    return vaddvq_u16(
        vaddq_u16(vshrq_n_u16(vcleq_u16(lo, bound), 15), vshrq_n_u16(vcleq_u16(hi, bound), 15)));
#elif defined(__AVX2__)
    auto sums = _mm256_cvtepu8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(widths)));
    sums = _mm256_add_epi16(sums, _mm256_slli_si256(sums, 2));
    sums = _mm256_add_epi16(sums, _mm256_slli_si256(sums, 4));
    sums = _mm256_add_epi16(sums, _mm256_slli_si256(sums, 8));
    const auto carry = _mm_set1_epi16(_mm_extract_epi16(_mm256_castsi256_si128(sums), 7));
    sums = _mm256_add_epi16(sums, _mm256_inserti128_si256(_mm256_setzero_si256(), carry, 1));
    const auto greater = _mm256_cmpgt_epi16(sums, _mm256_set1_epi16(offset));
    return std::countr_zero(unsigned(_mm256_movemask_epi8(greater))) / 2;
#else
    unsigned end = 0;
    for (unsigned r = 0; r < 16; ++r) {
        end += widths[r];
        if (offset < end)
            return r;
    }
    return 16;
#endif
}
} // namespace ikea::streampack::detail
