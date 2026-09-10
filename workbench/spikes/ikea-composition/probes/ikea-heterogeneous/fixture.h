#pragma once
#include "source.h"
#include <bit>
#include <cstring>

namespace ikea::heterogeneous {
inline std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
inline std::vector<PlainBlock> structural_blocks(unsigned count,
                                                 unsigned seed = 0) {
  std::vector<PlainBlock> blocks(count);
  for (unsigned i = 0; i < count; ++i) {
    unsigned population = (i + seed) % 257;
    for (unsigned j = 0; j < population; ++j) {
      const unsigned pos = (j * 157 + (i * 11 + seed)) % 256;
      blocks[i][pos / 8] |= 1u << (pos % 8);
    }
  }
  return blocks;
}
inline std::shared_ptr<const AlignedBytes> make_query(unsigned count,
                                                      std::uint64_t seed) {
  auto query = std::make_shared<AlignedBytes>(count * 32);
  for (unsigned i = 0; i < count * 32; ++i)
    query->data()[i] = mix(seed + i);
  return query;
}
inline std::uint64_t reference_count(std::span<const PlainBlock> plain,
                                     const AlignedBytes &query, unsigned first,
                                     unsigned count) {
  std::uint64_t result = 0;
  for (unsigned i = first; i < first + count; ++i)
    for (unsigned j = 0; j < 32; ++j)
      result +=
          std::popcount(std::uint8_t(plain[i][j] & query.data()[i * 32 + j]));
  return result;
}
} // namespace ikea::heterogeneous
