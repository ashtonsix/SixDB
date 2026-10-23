// The few vector primitives the body leaves are written in: 64-byte loads,
// byte sums, byte comparisons to row bits and row bits to byte masks. One
// implementation per ISA; the leaves above them are ISA-agnostic.
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

#if defined(__AVX512BW__)
#include <immintrin.h>
#define BODIES_ISA_AVX512 1
#elif defined(__AVX2__)
#include <immintrin.h>
#define BODIES_ISA_AVX2 1
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#define BODIES_ISA_NEON 1
#else
#error "bodies needs AVX-512BW, AVX2 or NEON"
#endif

namespace bodies {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

inline constexpr unsigned block_rows = 256;

#if BODIES_ISA_AVX512
inline const char *isa_name() { return "avx512"; }
#elif BODIES_ISA_AVX2
inline const char *isa_name() { return "avx2"; }
#else
inline const char *isa_name() { return "neon"; }
#endif

template <class T> inline T load_le(const void *p) {
    T x;
    std::memcpy(&x, p, sizeof(T));
    return x;
}

namespace simd {

typedef u8 b16 __attribute__((ext_vector_type(16)));
// t[i] for 16 indices i < 32 into a 32-byte table.
inline b16 lookup32(const u8 *t, b16 i) {
#if BODIES_ISA_AVX512 && defined(__AVX512VBMI__)
    __m128i lo, hi, x;
    std::memcpy(&lo, t, 16), std::memcpy(&hi, t + 16, 16), std::memcpy(&x, &i, 16);
    return __builtin_bit_cast(b16, _mm_permutex2var_epi8(lo, x, hi));
#elif BODIES_ISA_AVX512 || BODIES_ISA_AVX2
    __m128i lo, hi, x;
    std::memcpy(&lo, t, 16), std::memcpy(&hi, t + 16, 16), std::memcpy(&x, &i, 16);
    const __m128i high = _mm_slli_epi16(x, 3);  // bit 4 to the byte's sign bit
    return __builtin_bit_cast(b16, _mm_blendv_epi8(_mm_shuffle_epi8(lo, x), _mm_shuffle_epi8(hi, x), high));
#else
    uint8x16x2_t tab = {{vld1q_u8(t), vld1q_u8(t + 16)}};
    return __builtin_bit_cast(b16, vqtbl2q_u8(tab, __builtin_bit_cast(uint8x16_t, i)));
#endif
}

// 64 bytes: rows 0..63 of one byte lane.
#if BODIES_ISA_AVX512
struct v64 {
    __m512i x;
};
inline v64 load(const u8 *p) { return {_mm512_loadu_si512(p)}; }
// Byte-sum accumulator: u64 lanes.
struct acc {
    __m512i x = _mm512_setzero_si512();
    void add(v64 v) { x = _mm512_add_epi64(x, _mm512_sad_epu8(v.x, _mm512_setzero_si512())); }
    void add_masked(v64 v, u64 rows) {
        x = _mm512_add_epi64(x, _mm512_sad_epu8(_mm512_maskz_mov_epi8(rows, v.x), _mm512_setzero_si512()));
    }
    u64 total() const { return static_cast<u64>(_mm512_reduce_add_epi64(x)); }
};
inline u64 ge(v64 v, u8 c) { return _mm512_cmpge_epu8_mask(v.x, _mm512_set1_epi8(static_cast<char>(c))); }
inline u64 gt(v64 v, u8 c) { return _mm512_cmpgt_epu8_mask(v.x, _mm512_set1_epi8(static_cast<char>(c))); }
inline u64 eq(v64 v, u8 c) { return _mm512_cmpeq_epu8_mask(v.x, _mm512_set1_epi8(static_cast<char>(c))); }
// Bytes 0xff where the row bit is set.
inline v64 expand(u64 rows) { return {_mm512_movm_epi8(rows)}; }
inline v64 and_(v64 a, v64 b) { return {_mm512_and_si512(a.x, b.x)}; }
inline v64 or_(v64 a, v64 b) { return {_mm512_or_si512(a.x, b.x)}; }
inline v64 splat(u8 c) { return {_mm512_set1_epi8(static_cast<char>(c))}; }
inline v64 zero() { return {_mm512_setzero_si512()}; }
inline void store(u8 *p, v64 v) { _mm512_storeu_si512(p, v.x); }
// out[i] = v[i] << shift widened to u32 (store_widen32), or OR'd into out.
inline void widen32(__m512i (&w)[4], v64 v) {
    w[0] = _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(v.x, 0));
    w[1] = _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(v.x, 1));
    w[2] = _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(v.x, 2));
    w[3] = _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(v.x, 3));
}
inline void store_widen32(u32 *out, v64 v, unsigned shift) {
    const __m128i s = _mm_cvtsi32_si128(static_cast<int>(shift));
    __m512i w[4];
    widen32(w, v);
    for (unsigned q = 0; q < 4; ++q)
        _mm512_storeu_si512(out + 16 * q, _mm512_sll_epi32(w[q], s));
}
inline void or_widen32(u32 *out, v64 v, unsigned shift) {
    const __m128i s = _mm_cvtsi32_si128(static_cast<int>(shift));
    __m512i w[4];
    widen32(w, v);
    for (unsigned q = 0; q < 4; ++q)
        _mm512_storeu_si512(out + 16 * q, _mm512_or_si512(_mm512_loadu_si512(out + 16 * q), _mm512_sll_epi32(w[q], s)));
}
#elif BODIES_ISA_AVX2
struct v64 {
    __m256i a, b;
};
inline v64 load(const u8 *p) {
    return {_mm256_loadu_si256(reinterpret_cast<const __m256i *>(p)),
            _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + 32))};
}
inline u64 bits(__m256i a, __m256i b) {
    return static_cast<u32>(_mm256_movemask_epi8(a)) | u64{static_cast<u32>(_mm256_movemask_epi8(b))} << 32;
}
// Bytes 0xff where the row bit is set: each byte picks its bit's byte, then
// tests the bit.
inline __m256i expand32(u32 rows) {
    const __m256i r = _mm256_set1_epi32(static_cast<int>(rows));
    const __m256i pick = _mm256_setr_epi8(0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3,
                                          3, 3, 3, 3, 3, 3);
    const __m256i bit = _mm256_set1_epi64x(static_cast<long long>(0x8040201008040201ull));
    const __m256i x = _mm256_and_si256(_mm256_shuffle_epi8(r, pick), bit);
    return _mm256_cmpeq_epi8(x, bit);
}
inline v64 expand(u64 rows) { return {expand32(static_cast<u32>(rows)), expand32(static_cast<u32>(rows >> 32))}; }
struct acc {
    __m256i x = _mm256_setzero_si256();
    void add(v64 v) {
        const __m256i z = _mm256_setzero_si256();
        x = _mm256_add_epi64(x, _mm256_add_epi64(_mm256_sad_epu8(v.a, z), _mm256_sad_epu8(v.b, z)));
    }
    void add_masked(v64 v, u64 rows) {
        const v64 m = expand(rows);
        add({_mm256_and_si256(v.a, m.a), _mm256_and_si256(v.b, m.b)});
    }
    u64 total() const {
        const __m128i s = _mm_add_epi64(_mm256_castsi256_si128(x), _mm256_extracti128_si256(x, 1));
        return static_cast<u64>(_mm_cvtsi128_si64(s)) + static_cast<u64>(_mm_extract_epi64(s, 1));
    }
};
inline u64 ge(v64 v, u8 c) {
    const __m256i k = _mm256_set1_epi8(static_cast<char>(c));
    return bits(_mm256_cmpeq_epi8(_mm256_max_epu8(v.a, k), v.a), _mm256_cmpeq_epi8(_mm256_max_epu8(v.b, k), v.b));
}
inline u64 eq(v64 v, u8 c) {
    const __m256i k = _mm256_set1_epi8(static_cast<char>(c));
    return bits(_mm256_cmpeq_epi8(v.a, k), _mm256_cmpeq_epi8(v.b, k));
}
// Unsigned > as signed > with the sign bits flipped.
inline u64 gt(v64 v, u8 c) {
    const __m256i flip = _mm256_set1_epi8(static_cast<char>(0x80)), k = _mm256_set1_epi8(static_cast<char>(c ^ 0x80));
    return bits(_mm256_cmpgt_epi8(_mm256_xor_si256(v.a, flip), k), _mm256_cmpgt_epi8(_mm256_xor_si256(v.b, flip), k));
}
inline v64 and_(v64 a, v64 b) { return {_mm256_and_si256(a.a, b.a), _mm256_and_si256(a.b, b.b)}; }
inline v64 or_(v64 a, v64 b) { return {_mm256_or_si256(a.a, b.a), _mm256_or_si256(a.b, b.b)}; }
inline v64 splat(u8 c) { return {_mm256_set1_epi8(static_cast<char>(c)), _mm256_set1_epi8(static_cast<char>(c))}; }
inline v64 zero() { return {_mm256_setzero_si256(), _mm256_setzero_si256()}; }
inline void store(u8 *p, v64 v) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(p), v.a);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(p + 32), v.b);
}
inline void store_widen32(u32 *out, v64 v, unsigned shift) {
    const __m128i s = _mm_cvtsi32_si128(static_cast<int>(shift));
    const __m128i q[4] = {_mm256_castsi256_si128(v.a), _mm256_extracti128_si256(v.a, 1), _mm256_castsi256_si128(v.b),
                          _mm256_extracti128_si256(v.b, 1)};
    for (unsigned i = 0; i < 4; ++i) {
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(out + 16 * i), _mm256_sll_epi32(_mm256_cvtepu8_epi32(q[i]), s));
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(out + 16 * i + 8),
                            _mm256_sll_epi32(_mm256_cvtepu8_epi32(_mm_srli_si128(q[i], 8)), s));
    }
}
inline void or_widen32(u32 *out, v64 v, unsigned shift) {
    const __m128i s = _mm_cvtsi32_si128(static_cast<int>(shift));
    const __m128i q[4] = {_mm256_castsi256_si128(v.a), _mm256_extracti128_si256(v.a, 1), _mm256_castsi256_si128(v.b),
                          _mm256_extracti128_si256(v.b, 1)};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned h = 0; h < 2; ++h) {
            auto *o = reinterpret_cast<__m256i *>(out + 16 * i + 8 * h);
            const __m256i w = _mm256_sll_epi32(_mm256_cvtepu8_epi32(h ? _mm_srli_si128(q[i], 8) : q[i]), s);
            _mm256_storeu_si256(o, _mm256_or_si256(_mm256_loadu_si256(o), w));
        }
}
#else
struct v64 {
    uint8x16x4_t x;
};
inline v64 load(const u8 *p) { return {vld1q_u8_x4(p)}; }
// Pairwise-accumulated u16 lanes, four independent chains; a leaf adds at
// most 128 vectors per chain before total() (2 * 255 * 128 < 2^16).
struct acc {
    uint16x8_t x[4] = {vdupq_n_u16(0), vdupq_n_u16(0), vdupq_n_u16(0), vdupq_n_u16(0)};
    void add(v64 v) {
        for (unsigned q = 0; q < 4; ++q)
            x[q] = vpadalq_u8(x[q], v.x.val[q]);
    }
    inline void add_masked(v64 v, u64 rows);
    u64 total() const {
        return u64{vaddlvq_u16(x[0])} + vaddlvq_u16(x[1]) + vaddlvq_u16(x[2]) + vaddlvq_u16(x[3]);
    }
};
// Four 16-byte 0/0xff masks to 64 row bits.
inline u64 bits(uint8x16_t a, uint8x16_t b, uint8x16_t c, uint8x16_t d) {
    const uint8x16_t w = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    uint8x16_t ab = vpaddq_u8(vandq_u8(a, w), vandq_u8(b, w));
    uint8x16_t cd = vpaddq_u8(vandq_u8(c, w), vandq_u8(d, w));
    uint8x16_t abcd = vpaddq_u8(ab, cd);
    return vgetq_lane_u64(vreinterpretq_u64_u8(vpaddq_u8(abcd, abcd)), 0);
}
inline u64 ge(v64 v, u8 c) {
    const uint8x16_t k = vdupq_n_u8(c);
    return bits(vcgeq_u8(v.x.val[0], k), vcgeq_u8(v.x.val[1], k), vcgeq_u8(v.x.val[2], k), vcgeq_u8(v.x.val[3], k));
}
inline u64 gt(v64 v, u8 c) {
    const uint8x16_t k = vdupq_n_u8(c);
    return bits(vcgtq_u8(v.x.val[0], k), vcgtq_u8(v.x.val[1], k), vcgtq_u8(v.x.val[2], k), vcgtq_u8(v.x.val[3], k));
}
inline u64 eq(v64 v, u8 c) {
    const uint8x16_t k = vdupq_n_u8(c);
    return bits(vceqq_u8(v.x.val[0], k), vceqq_u8(v.x.val[1], k), vceqq_u8(v.x.val[2], k), vceqq_u8(v.x.val[3], k));
}
inline v64 expand(u64 rows) {
    const uint8x16_t b = vreinterpretq_u8_u64(vdupq_n_u64(rows));
    const uint8x16_t w = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    v64 r;
    for (unsigned q = 0; q < 4; ++q) {
        const uint8x16_t pick = {u8(2 * q), u8(2 * q), u8(2 * q), u8(2 * q), u8(2 * q), u8(2 * q), u8(2 * q), u8(2 * q),
                                 u8(2 * q + 1), u8(2 * q + 1), u8(2 * q + 1), u8(2 * q + 1), u8(2 * q + 1),
                                 u8(2 * q + 1), u8(2 * q + 1), u8(2 * q + 1)};
        r.x.val[q] = vtstq_u8(vqtbl1q_u8(b, pick), w);
    }
    return r;
}
inline void acc::add_masked(v64 v, u64 rows) {
    const v64 m = expand(rows);
    for (unsigned q = 0; q < 4; ++q)
        x[q] = vpadalq_u8(x[q], vandq_u8(v.x.val[q], m.x.val[q]));
}
inline v64 and_(v64 a, v64 b) {
    v64 r;
    for (unsigned q = 0; q < 4; ++q)
        r.x.val[q] = vandq_u8(a.x.val[q], b.x.val[q]);
    return r;
}
inline v64 or_(v64 a, v64 b) {
    v64 r;
    for (unsigned q = 0; q < 4; ++q)
        r.x.val[q] = vorrq_u8(a.x.val[q], b.x.val[q]);
    return r;
}
inline v64 splat(u8 c) { return {{vdupq_n_u8(c), vdupq_n_u8(c), vdupq_n_u8(c), vdupq_n_u8(c)}}; }
inline v64 zero() { return splat(0); }
inline void store(u8 *p, v64 v) { vst1q_u8_x4(p, v.x); }
inline void store_widen32(u32 *out, v64 v, unsigned shift) {
    const int32x4_t s = vdupq_n_s32(static_cast<int>(shift));
    for (unsigned q = 0; q < 4; ++q) {
        const uint16x8_t lo = vmovl_u8(vget_low_u8(v.x.val[q])), hi = vmovl_u8(vget_high_u8(v.x.val[q]));
        vst1q_u32(out + 16 * q, vshlq_u32(vmovl_u16(vget_low_u16(lo)), s));
        vst1q_u32(out + 16 * q + 4, vshlq_u32(vmovl_u16(vget_high_u16(lo)), s));
        vst1q_u32(out + 16 * q + 8, vshlq_u32(vmovl_u16(vget_low_u16(hi)), s));
        vst1q_u32(out + 16 * q + 12, vshlq_u32(vmovl_u16(vget_high_u16(hi)), s));
    }
}
inline void or_widen32(u32 *out, v64 v, unsigned shift) {
    const int32x4_t s = vdupq_n_s32(static_cast<int>(shift));
    for (unsigned q = 0; q < 4; ++q) {
        const uint16x8_t lo = vmovl_u8(vget_low_u8(v.x.val[q])), hi = vmovl_u8(vget_high_u8(v.x.val[q]));
        const uint32x4_t w[4] = {vmovl_u16(vget_low_u16(lo)), vmovl_u16(vget_high_u16(lo)), vmovl_u16(vget_low_u16(hi)),
                                 vmovl_u16(vget_high_u16(hi))};
        for (unsigned i = 0; i < 4; ++i)
            vst1q_u32(out + 16 * q + 4 * i, vorrq_u32(vld1q_u32(out + 16 * q + 4 * i), vshlq_u32(w[i], s)));
    }
}
#endif
}  // namespace simd
}  // namespace bodies
