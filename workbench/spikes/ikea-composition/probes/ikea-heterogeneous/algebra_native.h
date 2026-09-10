#pragma once
#include <cstdint>
#if defined(__aarch64__)
#include "../ikea-blocks/native_neon.h"
#else
#include "../ikea-blocks/native_avx512.h"
#endif

namespace ikea::heterogeneous::algebra {
// Inline-only native carriers. Pair half 0 belongs to the first supplied slice,
// half 1 to the second; neither slice ordinals nor addresses must be adjacent.
// No ownership or opaque-call convention is attached to these aliases.
#if defined(__aarch64__)
using Native1 = uint8x16x2_t;
using Native2 = uint8x16x4_t;
#else
using Native1 = __m256i;
using Native2 = __m512i;
#endif

// Plain inputs and outputs cover exactly 32 bytes per slice, with no alignment
// requirement. Each trusted BEC body/population pair has 64 readable bytes.
inline __attribute__((always_inline)) Native1
load_plain1(const std::uint8_t* p32) noexcept {
#if defined(__aarch64__)
    return {{vld1q_u8(p32), vld1q_u8(p32 + 16)}};
#else
    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p32));
#endif
}

inline __attribute__((always_inline)) Native1
decode_bec1(const std::uint8_t* p64, unsigned population) noexcept {
    unsigned used_bits;
#if defined(__aarch64__)
    return ikea_probe::neon::decode(p64, population, used_bits);
#else
    return ikea_probe::avx512::decode(p64, population, used_bits);
#endif
}

inline __attribute__((always_inline)) Native2
load_plain2(const std::uint8_t* a32, const std::uint8_t* b32) noexcept {
#if defined(__aarch64__)
    return {{vld1q_u8(a32), vld1q_u8(a32 + 16),
             vld1q_u8(b32), vld1q_u8(b32 + 16)}};
#else
    return _mm512_inserti64x4(_mm512_castsi256_si512(load_plain1(a32)),
                              load_plain1(b32), 1);
#endif
}

inline __attribute__((always_inline)) Native2
decode_bec2(const std::uint8_t* a64, unsigned pop_a,
            const std::uint8_t* b64, unsigned pop_b) noexcept {
#if defined(__aarch64__)
    const auto a = decode_bec1(a64, pop_a);
    const auto b = decode_bec1(b64, pop_b);
    return {{a.val[0], a.val[1], b.val[0], b.val[1]}};
#else
    unsigned bits_a, bits_b;
    return ikea_probe::avx512::decode2(a64, pop_a, b64, pop_b, bits_a, bits_b);
#endif
}

template<bool Union>
inline __attribute__((always_inline)) Native1
combine1(Native1 a, Native1 b) noexcept {
#if defined(__aarch64__)
    if constexpr (Union)
        return {{vorrq_u8(a.val[0], b.val[0]), vorrq_u8(a.val[1], b.val[1])}};
    else
        return {{vandq_u8(a.val[0], b.val[0]), vandq_u8(a.val[1], b.val[1])}};
#else
    if constexpr (Union) return _mm256_or_si256(a, b);
    else return _mm256_and_si256(a, b);
#endif
}

template<bool Union>
inline __attribute__((always_inline)) Native2
combine2(Native2 a, Native2 b) noexcept {
#if defined(__aarch64__)
    if constexpr (Union)
        return {{vorrq_u8(a.val[0], b.val[0]), vorrq_u8(a.val[1], b.val[1]),
                 vorrq_u8(a.val[2], b.val[2]), vorrq_u8(a.val[3], b.val[3])}};
    else
        return {{vandq_u8(a.val[0], b.val[0]), vandq_u8(a.val[1], b.val[1]),
                 vandq_u8(a.val[2], b.val[2]), vandq_u8(a.val[3], b.val[3])}};
#else
    if constexpr (Union) return _mm512_or_si512(a, b);
    else return _mm512_and_si512(a, b);
#endif
}

// An isolated selected slice may use the two decoder domains for its two
// sources. Combine those domains into one result without decoding a neighbour.
template<bool Union>
inline __attribute__((always_inline)) Native1
combine_halves(Native2 value) noexcept {
#if defined(__aarch64__)
    return combine1<Union>({{value.val[0], value.val[1]}},
                            {{value.val[2], value.val[3]}});
#else
    return combine1<Union>(_mm512_castsi512_si256(value),
                            _mm512_extracti64x4_epi64(value, 1));
#endif
}

inline __attribute__((always_inline)) void
store1(std::uint8_t* out32, Native1 value) noexcept {
#if defined(__aarch64__)
    vst1q_u8(out32, value.val[0]);
    vst1q_u8(out32 + 16, value.val[1]);
#else
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out32), value);
#endif
}

// The two writable slice ranges must be disjoint. Their address order is free.
inline __attribute__((always_inline)) void
store2(std::uint8_t* out_a32, std::uint8_t* out_b32, Native2 value) noexcept {
#if defined(__aarch64__)
    vst1q_u8(out_a32, value.val[0]);
    vst1q_u8(out_a32 + 16, value.val[1]);
    vst1q_u8(out_b32, value.val[2]);
    vst1q_u8(out_b32 + 16, value.val[3]);
#else
    store1(out_a32, _mm512_castsi512_si256(value));
    store1(out_b32, _mm512_extracti64x4_epi64(value, 1));
#endif
}
}

int check_algebra_native();
