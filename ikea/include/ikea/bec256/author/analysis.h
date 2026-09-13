#pragma once
#include <ikea/bec256/analysis.h>
#include <ikea/bec256/author/native.h>
#include <ikea/bec256/detail/estimate.h>

namespace ikea::bec256::detail {
#if defined(IKEA_BEC256_AVX512)
[[gnu::always_inline]] inline unsigned sum_bytes(__m256i bytes) {
    const auto words = _mm256_sad_epu8(bytes, _mm256_setzero_si256());
    const auto halves =
        _mm_add_epi64(_mm256_castsi256_si128(words), _mm256_extracti128_si256(words, 1));
    return _mm_cvtsi128_si64(halves) + _mm_extract_epi64(halves, 1);
}
[[gnu::always_inline]] inline size_features features_native(native::block bits) {
    const auto counts = _mm256_popcnt_epi8(bits);
    const auto widths = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(byte_width.data())));
    const auto quarters = _mm256_sad_epu8(counts, _mm256_setzero_si256());
    const unsigned pop = sum_bytes(counts);
    // Shift across all four words, excluding the final outer boundary.
    const auto following = _mm256_permute4x64_epi64(bits, _MM_SHUFFLE(3, 3, 2, 1));
    const auto shifted =
        _mm256_or_si256(_mm256_srli_epi64(bits, 1), _mm256_slli_epi64(following, 63));
    const auto changed = _mm256_and_si256(_mm256_xor_si256(bits, shifted),
                                          _mm256_set_epi64x(0x7fffffffffffffffLL, -1, -1, -1));
    const auto diff = _mm256_sub_epi64(_mm256_slli_epi64(quarters, 2), _mm256_set1_epi64x(pop));
    const auto absolute = _mm256_abs_epi64(diff);
    const auto halves =
        _mm_add_epi64(_mm256_castsi256_si128(absolute), _mm256_extracti128_si256(absolute, 1));
    return {pop, sum_bytes(_mm256_shuffle_epi8(widths, counts)),
            sum_bytes(_mm256_popcnt_epi8(changed)),
            unsigned(_mm_cvtsi128_si64(halves) + _mm_extract_epi64(halves, 1))};
}
#elif defined(__aarch64__)
[[gnu::always_inline]] inline size_features features_native(native::block bits) {
    const auto lo = vcntq_u8(bits.val[0]), hi = vcntq_u8(bits.val[1]);
    const auto widths = vld1q_u8(byte_width.data());
    const unsigned pop = unsigned(vaddvq_u8(lo)) + vaddvq_u8(hi);
    const auto shift_lo =
        vorrq_u8(vshrq_n_u8(bits.val[0], 1), vshlq_n_u8(vextq_u8(bits.val[0], bits.val[1], 1), 7));
    const auto shift_hi = vorrq_u8(vshrq_n_u8(bits.val[1], 1),
                                   vshlq_n_u8(vextq_u8(bits.val[1], vdupq_n_u8(0), 1), 7));
    const auto mask = vsetq_lane_u8(127, vdupq_n_u8(255), 15);
    const auto trans_lo = vcntq_u8(veorq_u8(bits.val[0], shift_lo));
    const auto trans_hi = vcntq_u8(vandq_u8(veorq_u8(bits.val[1], shift_hi), mask));
    const auto quarters = vpaddq_u32(vpaddlq_u16(vpaddlq_u8(lo)), vpaddlq_u16(vpaddlq_u8(hi)));
    const auto diff = vsubq_s32(vreinterpretq_s32_u32(vshlq_n_u32(quarters, 2)), vdupq_n_s32(pop));
    return {pop, unsigned(vaddvq_u8(vqtbl1q_u8(widths, lo))) + vaddvq_u8(vqtbl1q_u8(widths, hi)),
            unsigned(vaddvq_u8(trans_lo)) + vaddvq_u8(trans_hi),
            vaddvq_u32(vreinterpretq_u32_s32(vabsq_s32(diff)))};
}
#endif
} // namespace ikea::bec256::detail

namespace ikea::bec256::native {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
/// Same approximate byte estimate as bec256::estimate_bytes, consuming native
/// bits directly. No materialization, writes, effects or retained references.
[[nodiscard, gnu::always_inline]] inline unsigned estimate_bytes(block value) noexcept {
    return detail::estimate(detail::features_native(value));
}
/// Estimates each 256-position half separately; this does not estimate a joint
/// wire format or include the owner's directory bytes.
[[nodiscard, gnu::always_inline]] inline std::array<unsigned, 2>
estimate_bytes(pair value) noexcept {
    return {estimate_bytes(part<0>(value)), estimate_bytes(part<1>(value))};
}
/// Byte-enumeration cost in bits; excludes the population tree. This is the
/// statistic used by encode_if_promising, not a compressed byte count.
[[nodiscard, gnu::always_inline]] inline unsigned enum_bits(block value) noexcept {
#if defined(IKEA_BEC256_AVX512)
    const auto widths = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(detail::byte_width.data())));
    return detail::sum_bytes(_mm256_shuffle_epi8(widths, _mm256_popcnt_epi8(value)));
#else
    const auto widths = vld1q_u8(detail::byte_width.data());
    return unsigned(vaddvq_u8(vqtbl1q_u8(widths, vcntq_u8(value.val[0])))) +
           vaddvq_u8(vqtbl1q_u8(widths, vcntq_u8(value.val[1])));
#endif
}
[[nodiscard, gnu::always_inline]] inline std::array<unsigned, 2> enum_bits(pair value) noexcept {
    return {enum_bits(part<0>(value)), enum_bits(part<1>(value))};
}
#endif
} // namespace ikea::bec256::native
