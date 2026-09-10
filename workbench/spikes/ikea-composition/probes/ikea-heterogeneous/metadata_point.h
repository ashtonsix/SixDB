#pragma once
#include "metadata_native.h"
#include <bit>
#include <cstring>

namespace ikea::heterogeneous {
// Trusted single-record resolution over the existing bytes. Predecessor
// lengths remain necessary even when their bodies are excluded by a mask.
template<MetadataKind Kind>
inline __attribute__((always_inline)) std::uint32_t
read_metadata_point(const std::uint8_t *base, unsigned index) {
  if constexpr (Kind == MetadataKind::direct32) {
    std::uint32_t word;
    std::memcpy(&word, base + 4 * index, 4);
    return ((word >> 15) << 16) | (word & 511);
  } else {
    const unsigned group = index / 16, lane = index % 16;
    const auto *pop = Kind == MetadataKind::local16
        ? base + 32 * group + 2 : base + 32 + 18 * group;
    const auto *checkpoint = Kind == MetadataKind::local16
        ? base + 32 * group : base + 2 * group;
    unsigned offset = checkpoint[0] | unsigned(checkpoint[1]) << 8;
    const unsigned population = 2 * pop[lane] + ((pop[16 + lane / 8] >> (lane % 8)) & 1);
    if constexpr (Kind == MetadataKind::local16) {
      const auto *lengths = base + 32 * group + 20;
      const unsigned preceding = (1u << lane) - 1;
      for (unsigned bit = 0; bit < 6; ++bit)
        offset += std::popcount((unsigned(lengths[bit]) |
            unsigned(lengths[bit + 6]) << 8) & preceding) << bit;
    } else {
      const auto values = ikea::integers::scan_read16<6>(base + 320, group * 16);
#if defined(__aarch64__)
      const std::uint8_t indices[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
      offset += vaddlvq_u8(vandq_u8(values.v, vcltq_u8(vld1q_u8(indices), vdupq_n_u8(lane))));
#else
      const auto indices = _mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
      const auto sums = _mm_sad_epu8(_mm_and_si128(values.v,
          _mm_cmpgt_epi8(_mm_set1_epi8(lane), indices)), _mm_setzero_si128());
      offset += unsigned(_mm_cvtsi128_si64(sums) + _mm_extract_epi64(sums, 1));
#endif
    }
    return (offset << 16) | population;
  }
}
} // namespace ikea::heterogeneous
