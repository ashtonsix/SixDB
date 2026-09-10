#pragma once
#include <cstdint>
#if defined(__aarch64__)
#include "../ikea-blocks/native_neon.h"
#else
#include "../ikea-blocks/native_avx512.h"
#endif

namespace ikea::heterogeneous {
// Trusted body/population pairs, each with 64 readable bytes. Query masks are
// exact 32-byte blocks in the same logical coordinates as their decoded body.
// No alignment requirement, ownership transfer, output buffer, or validation.
inline __attribute__((always_inline)) std::uint64_t bec_count1_inline(
    const std::uint8_t* body, unsigned population,
    const std::uint8_t* query32) noexcept {
    unsigned used_bits;
#if defined(__aarch64__)
    const auto bits = ikea_probe::neon::decode(body, population, used_bits);
    const auto lo = vcntq_u8(vandq_u8(bits.val[0], vld1q_u8(query32)));
    const auto hi = vcntq_u8(vandq_u8(bits.val[1], vld1q_u8(query32 + 16)));
    return vaddlvq_u8(vaddq_u8(lo, hi));
#else
    const auto bits = ikea_probe::avx512::decode(body, population, used_bits);
    const auto count = _mm256_popcnt_epi64(_mm256_and_si256(
        bits, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(query32))));
    const auto pair = _mm_add_epi64(_mm256_castsi256_si128(count),
                                  _mm256_extracti128_si256(count, 1));
    return std::uint64_t(_mm_cvtsi128_si64(
        _mm_add_epi64(pair, _mm_srli_si128(pair, 8))));
#endif
}

// Independent body addresses; query64 holds their two consecutive query
// blocks. The result is the sum of two intersections, without cross-block AND.
inline __attribute__((always_inline)) std::uint64_t bec_count2_inline(
    const std::uint8_t* a, unsigned pop_a,
    const std::uint8_t* b, unsigned pop_b,
    const std::uint8_t* query64) noexcept {
#if defined(__aarch64__)
    // Reuse the one-body native recipe for each independent NEON stream.
    return bec_count1_inline(a, pop_a, query64) +
           bec_count1_inline(b, pop_b, query64 + 32);
#else
    unsigned bits_a, bits_b;
    const auto bits = ikea_probe::avx512::decode2(a, pop_a, b, pop_b, bits_a, bits_b);
    const auto count = _mm512_popcnt_epi64(
        _mm512_and_si512(bits, _mm512_loadu_si512(query64)));
    return _mm512_reduce_add_epi64(count);
#endif
}

extern "C" std::uint64_t ikea_heterogeneous_bec_count1(
    const std::uint8_t* body, unsigned population,
    const std::uint8_t* query32) noexcept;
extern "C" std::uint64_t ikea_heterogeneous_bec_count2(
    const std::uint8_t* a, unsigned pop_a,
    const std::uint8_t* b, unsigned pop_b,
    const std::uint8_t* query64) noexcept;
}

// Cold checks, implemented independently from the native region TU.
int check_bec_regions();
