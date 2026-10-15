#pragma once
#include <stdint.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#else
#error Eager S6 requires explicit NEON or AVX2; no scalar fallback.
#endif

// Exploratory complete-key match for one initialized 64-byte key line:
// count at byte 0 (0..8), fingerprints at 8..15, eight LE48 suffixes at 16..63.
// All inactive fingerprints are 255. Unused suffix bits may contain anything.
// mask is a low-bit mask of 0..48 bits. The query suffix is masked here too.
// Every issued payload load is entirely within [key_line,key_line+64).
// No alignment requirement. Returns ALL exact matches as dense bits 0..7.
// A table with unique keys normally has at most one match; first() also handles
// zero/multiple matches and returns 8 when none. Count is only needed for fp255.
namespace hashtable_eager {
inline constexpr uint64_t storage_mask = 0x0000ffffffffffffULL;
[[gnu::always_inline]] inline unsigned first(unsigned candidates) {
    return candidates ? unsigned(__builtin_ctz(candidates)) : 8;
}

[[gnu::always_inline]] inline unsigned candidates_s6(
    const uint8_t* key_line, uint8_t fingerprint, uint64_t suffix, uint64_t mask) {
    suffix &= mask;
    unsigned matches;
#if defined(__aarch64__)
    // Four overlapping bounded loads admit cheaper TBL1 operations than a
    // three-register gather. The last pair loads at +48, never +52.
    const uint8x16_t pair_index{0,1,2,3,4,5,255,255,6,7,8,9,10,11,255,255};
    const uint8x16_t last_index{4,5,6,7,8,9,255,255,10,11,12,13,14,15,255,255};
    const auto s01 = vreinterpretq_u64_u8(vqtbl1q_u8(vld1q_u8(key_line+16),pair_index));
    const auto s23 = vreinterpretq_u64_u8(vqtbl1q_u8(vld1q_u8(key_line+28),pair_index));
    const auto s45 = vreinterpretq_u64_u8(vqtbl1q_u8(vld1q_u8(key_line+40),pair_index));
    const auto s67 = vreinterpretq_u64_u8(vqtbl1q_u8(vld1q_u8(key_line+48),last_index));
    const auto wanted=vdupq_n_u64(suffix), keep=vdupq_n_u64(mask);
    const auto e01=vceqq_u64(vandq_u64(s01,keep),wanted);
    const auto e23=vceqq_u64(vandq_u64(s23,keep),wanted);
    const auto e45=vceqq_u64(vandq_u64(s45,keep),wanted);
    const auto e67=vceqq_u64(vandq_u64(s67,keep),wanted);
    const auto e03=vuzp1q_u32(vreinterpretq_u32_u64(e01),vreinterpretq_u32_u64(e23));
    const auto e47=vuzp1q_u32(vreinterpretq_u32_u64(e45),vreinterpretq_u32_u64(e67));
    const auto e07=vuzp1q_u16(vreinterpretq_u16_u32(e03),vreinterpretq_u16_u32(e47));
    const auto suffix_equal=vmovn_u16(e07);
    const auto fp_equal=vceq_u8(vld1_u8(key_line+8),vdup_n_u8(fingerprint));
    const uint8x8_t weights{1,2,4,8,16,32,64,128};
    matches=vaddv_u8(vand_u8(vand_u8(suffix_equal,fp_equal),weights));
#elif defined(__AVX512VBMI__) && defined(__AVX512BW__)
    // VPERMB gathers all six-byte fields from ONE exact64-byte load.
    // A byte mask zeroes the top two bytes of every decoded qword.
    alignas(64) static constexpr uint8_t index[64]{
        16,17,18,19,20,21,0,0,22,23,24,25,26,27,0,0,
        28,29,30,31,32,33,0,0,34,35,36,37,38,39,0,0,
        40,41,42,43,44,45,0,0,46,47,48,49,50,51,0,0,
        52,53,54,55,56,57,0,0,58,59,60,61,62,63,0,0};
    const auto key=_mm512_loadu_si512(key_line);
    const auto decoded=_mm512_maskz_permutexvar_epi8(
        0x3f3f3f3f3f3f3f3fULL,_mm512_load_si512(index),key);
    const unsigned sm=_mm512_cmpeq_epi64_mask(
        _mm512_and_si512(decoded,_mm512_set1_epi64(static_cast<long long>(mask))),
        _mm512_set1_epi64(static_cast<long long>(suffix)));
    const unsigned fm=unsigned(_mm512_cmpeq_epi8_mask(key,_mm512_set1_epi8(char(fingerprint)))>>8)&255;
    matches=sm&fm;
#else
    // Explicit AVX2 body, also available in AVX-512 builds lacking VBMI.
    // First32-byte window is suffix bytes0..31, second is bytes16..47.
    // VPERMQ supplies each 128-bit lane with its pair's twelve source bytes;
    // one lane-local VPSHUFB then zero-extends both six-byte fields.
    const auto index=_mm256_setr_epi8(
        0,1,2,3,4,5,-1,-1,6,7,8,9,10,11,-1,-1,
        4,5,6,7,8,9,-1,-1,10,11,12,13,14,15,-1,-1);
    const auto lo=_mm256_shuffle_epi8(_mm256_permute4x64_epi64(
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(key_line+16)),
        _MM_SHUFFLE(2,1,1,0)),index);
    const auto hi=_mm256_shuffle_epi8(_mm256_permute4x64_epi64(
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(key_line+32)),
        _MM_SHUFFLE(3,2,2,1)),index);
    const auto keep=_mm256_set1_epi64x(static_cast<long long>(mask));
    const auto wanted=_mm256_set1_epi64x(static_cast<long long>(suffix));
    const unsigned sl=unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(
        _mm256_cmpeq_epi64(_mm256_and_si256(lo,keep),wanted))));
    const unsigned sh=unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(
        _mm256_cmpeq_epi64(_mm256_and_si256(hi,keep),wanted))));
    const unsigned fm=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(
        _mm_loadl_epi64(reinterpret_cast<const __m128i*>(key_line+8)),
        _mm_set1_epi8(char(fingerprint)))))&255;
    matches=(sl|(sh<<4))&fm;
#endif
    if (fingerprint==255) matches&=(1u<<key_line[0])-1;
    return matches;
}
} // namespace hashtable_eager
