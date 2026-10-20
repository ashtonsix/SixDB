#pragma once
#include <cstddef>
#include <cstdint>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace ikea::boundlist::native {
using byte = std::byte;
#if defined(__AVX512F__) && defined(__AVX512BW__)
using vector = __m512i;
inline constexpr unsigned lanes = 16;
inline vector splat(unsigned x) {
    return _mm512_set1_epi32(x);
}
inline vector add(vector a, vector b) {
    return _mm512_add_epi32(a, b);
}
inline vector sub(vector a, vector b) {
    return _mm512_sub_epi32(a, b);
}
inline vector multiply(vector a, unsigned x) {
    return _mm512_mullo_epi32(a, splat(x));
}
inline vector indices() {
    return _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
}
inline unsigned last(vector x) {
    return _mm_extract_epi32(_mm512_extracti32x4_epi32(x, 3), 3);
}
inline vector previous(vector x, unsigned incoming) {
    return _mm512_mask_mov_epi32(
        _mm512_permutexvar_epi32(
            _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), x),
        1, splat(incoming));
}
inline vector prefix(vector x) {
    x = add(x,
            _mm512_maskz_permutexvar_epi32(
                0xfffe, _mm512_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14), x));
    x = add(x,
            _mm512_maskz_permutexvar_epi32(
                0xfffc, _mm512_setr_epi32(0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13), x));
    x = add(x, _mm512_maskz_permutexvar_epi32(
                   0xfff0, _mm512_setr_epi32(0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11), x));
    return add(x,
               _mm512_maskz_permutexvar_epi32(
                   0xff00, _mm512_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7), x));
}
template <class T> inline vector load(const byte* p) {
    if constexpr (sizeof(T) == 1)
        return _mm512_cvtepu8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    else if constexpr (sizeof(T) == 2)
        return _mm512_cvtepu16_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)));
    else
        return _mm512_loadu_si512(p);
}
inline void store(unsigned* p, vector x) {
    _mm512_storeu_si512(p, x);
}
inline unsigned greater(vector x, unsigned bound) {
    return _mm512_cmpgt_epu32_mask(x, splat(bound));
}
#elif defined(__AVX2__)
using vector = __m256i;
inline constexpr unsigned lanes = 8;
inline vector splat(unsigned x) {
    return _mm256_set1_epi32(x);
}
inline vector add(vector a, vector b) {
    return _mm256_add_epi32(a, b);
}
inline vector sub(vector a, vector b) {
    return _mm256_sub_epi32(a, b);
}
inline vector multiply(vector a, unsigned x) {
    return _mm256_mullo_epi32(a, splat(x));
}
inline vector indices() {
    return _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
}
inline unsigned last(vector x) {
    return _mm256_extract_epi32(x, 7);
}
inline vector previous(vector x, unsigned incoming) {
    return _mm256_blend_epi32(
        _mm256_permutevar8x32_epi32(x, _mm256_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6)), splat(incoming),
        1);
}
inline vector prefix(vector x) {
    x = add(x, _mm256_slli_si256(x, 4));
    x = add(x, _mm256_slli_si256(x, 8));
    return add(x, _mm256_blend_epi32(_mm256_setzero_si256(),
                                     _mm256_permutevar8x32_epi32(x, splat(3)), 0xf0));
}
template <class T> inline vector load(const byte* p) {
    if constexpr (sizeof(T) == 1)
        return _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    else if constexpr (sizeof(T) == 2)
        return _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    else
        return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
}
inline void store(unsigned* p, vector x) {
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), x);
}
inline unsigned greater(vector x, unsigned bound) {
    const auto sign = _mm256_set1_epi32(INT32_MIN);
    return _mm256_movemask_ps(_mm256_castsi256_ps(
        _mm256_cmpgt_epi32(_mm256_xor_si256(x, sign), _mm256_xor_si256(splat(bound), sign))));
}
#elif defined(__aarch64__)
using vector = uint32x4_t;
inline constexpr unsigned lanes = 4;
inline vector splat(unsigned x) {
    return vdupq_n_u32(x);
}
inline vector add(vector a, vector b) {
    return vaddq_u32(a, b);
}
inline vector sub(vector a, vector b) {
    return vsubq_u32(a, b);
}
inline vector multiply(vector a, unsigned x) {
    return vmulq_n_u32(a, x);
}
inline vector indices() {
    return {0, 1, 2, 3};
}
inline unsigned last(vector x) {
    return vgetq_lane_u32(x, 3);
}
inline vector previous(vector x, unsigned incoming) {
    return vextq_u32(splat(incoming), x, 3);
}
inline vector prefix(vector x) {
    const auto zero = splat(0);
    x = add(x, vextq_u32(zero, x, 3));
    return add(x, vextq_u32(zero, x, 2));
}
template <class T> inline vector load(const byte* p) {
    if constexpr (sizeof(T) == 1) {
        const auto bytes =
            vreinterpret_u8_u32(vld1_dup_u32(reinterpret_cast<const std::uint32_t*>(p)));
        return vmovl_u16(vget_low_u16(vmovl_u8(bytes)));
    } else if constexpr (sizeof(T) == 2)
        return vmovl_u16(vld1_u16(reinterpret_cast<const std::uint16_t*>(p)));
    else
        return vld1q_u32(reinterpret_cast<const std::uint32_t*>(p));
}
inline void store(unsigned* p, vector x) {
    vst1q_u32(p, x);
}
inline unsigned greater(vector x, unsigned bound) {
    return vaddvq_u32(vandq_u32(vcgtq_u32(x, splat(bound)), vector{1, 2, 4, 8}));
}
#endif

} // namespace ikea::boundlist::native
