#pragma once

#include "../predictor/model.h"
#include "contracts.h"
#include <cstdint>

#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#else
#error "This native composition probe supports x86 AVX2 or AArch64 NEON"
#endif

namespace ikea::composition {

struct FeaturePair { std::uint64_t first, second; };

#if defined(__x86_64__)
using Native256 = __m256i;
inline Native256 load_native(const void* p) { return _mm256_loadu_si256(static_cast<const __m256i*>(p)); }
inline Native256 zero_native() { return _mm256_setzero_si256(); }

inline __m256i byte_popcounts(__m256i bits) {
#if defined(__AVX512BITALG__) && defined(__AVX512VL__)
    return _mm256_popcnt_epi8(bits);
#else
    const auto lut = _mm256_setr_epi8(0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,
                                     0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4);
    const auto mask = _mm256_set1_epi8(15);
    return _mm256_add_epi8(_mm256_shuffle_epi8(lut, _mm256_and_si256(bits, mask)),
                          _mm256_shuffle_epi8(lut, _mm256_and_si256(_mm256_srli_epi16(bits,4), mask)));
#endif
}
inline std::uint64_t sum_four(__m256i words) {
    auto pair = _mm_add_epi64(_mm256_castsi256_si128(words), _mm256_extracti128_si256(words,1));
    pair = _mm_add_epi64(pair, _mm_srli_si128(pair,8));
    return static_cast<std::uint64_t>(_mm_cvtsi128_si64(pair));
}
inline std::uint64_t feature_sums(unsigned pop, unsigned cost) {
    return bec_predictor::pack_features(pop < 128 ? 128-pop : pop-128, cost);
}
template<bool Transitions, bool Quarters>
inline std::uint64_t native_features_impl(Native256 bits) {
    const auto counts = byte_popcounts(bits);
    const auto widths = _mm256_setr_epi8(0,3,5,6,7,6,5,3,0,0,0,0,0,0,0,0,
                                         0,3,5,6,7,6,5,3,0,0,0,0,0,0,0,0);
    const auto cost = _mm256_shuffle_epi8(widths, counts);
    const auto quarter_pops=_mm256_sad_epu8(counts,zero_native());
    const auto pop=static_cast<unsigned>(sum_four(quarter_pops));
    auto packed=feature_sums(pop,static_cast<unsigned>(sum_four(_mm256_sad_epu8(cost,zero_native()))));
    if constexpr(Transitions) {
        const auto following=_mm256_permute4x64_epi64(bits,_MM_SHUFFLE(3,3,2,1));
        const auto shifted=_mm256_or_si256(_mm256_srli_epi64(bits,1),_mm256_slli_epi64(following,63));
        const auto mask=_mm256_set_epi64x(0x7fffffffffffffffLL,-1,-1,-1);
        const auto transitions=_mm256_and_si256(_mm256_xor_si256(bits,shifted),mask);
        packed|=sum_four(_mm256_sad_epu8(byte_popcounts(transitions),zero_native()))<<8;
    }
    if constexpr(Quarters) {
        const auto difference=_mm256_sub_epi64(_mm256_slli_epi64(quarter_pops,2),_mm256_set1_epi64x(pop));
        const auto sign=_mm256_cmpgt_epi64(zero_native(),difference);
        const auto absolute=_mm256_sub_epi64(_mm256_xor_si256(difference,sign),sign);
        packed|=sum_four(absolute)<<24;
    }
    return packed;
}
inline std::uint64_t native_features(Native256 bits) { return native_features_impl<false,false>(bits); }

inline FeaturePair features_from_64_bytes(const void* p) {
#if defined(__AVX512BITALG__) && defined(__AVX512BW__)
    const auto bits = _mm512_loadu_si512(p);
    const auto counts = _mm512_popcnt_epi8(bits);
    const auto widths = _mm512_broadcast_i32x4(_mm_setr_epi8(0,3,5,6,7,6,5,3,0,0,0,0,0,0,0,0));
    const auto costs = _mm512_shuffle_epi8(widths, counts);
    const auto pops = _mm512_sad_epu8(counts, _mm512_setzero_si512());
    const auto sums = _mm512_sad_epu8(costs, _mm512_setzero_si512());
    return {feature_sums(static_cast<unsigned>(sum_four(_mm512_castsi512_si256(pops))),
                         static_cast<unsigned>(sum_four(_mm512_castsi512_si256(sums)))),
            feature_sums(static_cast<unsigned>(sum_four(_mm512_extracti64x4_epi64(pops,1))),
                         static_cast<unsigned>(sum_four(_mm512_extracti64x4_epi64(sums,1))))};
#else
    const auto* bytes = static_cast<const std::uint8_t*>(p);
    return {native_features(load_native(bytes)), native_features(load_native(bytes+32))};
#endif
}

#else
struct Native256 { uint8x16_t lo, hi; };
inline Native256 load_native(const void* p) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    return {vld1q_u8(b),vld1q_u8(b+16)};
}
inline Native256 zero_native() { return {vdupq_n_u8(0),vdupq_n_u8(0)}; }
template<bool Transitions, bool Quarters>
inline std::uint64_t native_features_impl(Native256 bits) {
    const uint8x16_t widths = {0,3,5,6,7,6,5,3,0,0,0,0,0,0,0,0};
    const auto lo = vcntq_u8(bits.lo);
    const auto hi = vcntq_u8(bits.hi);
    const unsigned pop = static_cast<unsigned>(vaddvq_u8(lo)) + vaddvq_u8(hi);
    const unsigned cost = static_cast<unsigned>(vaddvq_u8(vqtbl1q_u8(widths,lo))) +
                          vaddvq_u8(vqtbl1q_u8(widths,hi));
    auto packed=bec_predictor::pack_features(pop < 128 ? 128-pop : pop-128, cost);
    if constexpr(Transitions) {
        const auto shifted_lo=vorrq_u8(vshrq_n_u8(bits.lo,1),vshlq_n_u8(vextq_u8(bits.lo,bits.hi,1),7));
        const auto shifted_hi=vorrq_u8(vshrq_n_u8(bits.hi,1),vshlq_n_u8(vextq_u8(bits.hi,vdupq_n_u8(0),1),7));
        const uint8x16_t mask={255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,127};
        const auto trans_lo=vcntq_u8(veorq_u8(bits.lo,shifted_lo));
        const auto trans_hi=vcntq_u8(vandq_u8(veorq_u8(bits.hi,shifted_hi),mask));
        const unsigned transitions=static_cast<unsigned>(vaddvq_u8(trans_lo))+vaddvq_u8(trans_hi);
        packed|=std::uint64_t{transitions}<<8;
    }
    if constexpr(Quarters) {
        const auto quarter_pops=vpaddq_u32(vpaddlq_u16(vpaddlq_u8(lo)),vpaddlq_u16(vpaddlq_u8(hi)));
        const auto difference=vsubq_s32(vreinterpretq_s32_u32(vshlq_n_u32(quarter_pops,2)),vdupq_n_s32(pop));
        const auto dispersion=vaddvq_u32(vreinterpretq_u32_s32(vabsq_s32(difference)));
        packed|=std::uint64_t{dispersion}<<24;
    }
    return packed;
}
inline std::uint64_t native_features(Native256 bits) { return native_features_impl<false,false>(bits); }
inline FeaturePair features_from_64_bytes(const void* p) {
    const auto* bytes = static_cast<const std::uint8_t*>(p);
    return {native_features(load_native(bytes)),native_features(load_native(bytes+32))};
}
#endif

inline std::uint64_t features_from_bytes(const void* p) { return native_features(load_native(p)); }
inline std::uint64_t features_with_transitions(const void* p) { return native_features_impl<true,false>(load_native(p)); }
inline std::uint64_t features_with_quadrants(const void* p) { return native_features_impl<true,true>(load_native(p)); }

// Stable scalar/pointer wrappers for external benchmark drivers. All require
// exactly 32/64 readable bytes; the native bodies above are reusable inline.
extern "C" std::uint64_t ikea_comp_features256(const void*);
extern "C" FeaturePair ikea_comp_features512(const void*);
extern "C" unsigned ikea_comp_predict256(const void*);
extern "C" std::uint64_t ikea_comp_features_transitions(const void*);
extern "C" std::uint64_t ikea_comp_features_quadrants(const void*);
extern "C" unsigned ikea_comp_predict_transitions(const void*);
extern "C" unsigned ikea_comp_predict_quadrants(const void*);

} // namespace ikea::composition
