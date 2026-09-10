#pragma once
#include "metadata_format.h"
#include <cstdint>

namespace ikea::heterogeneous {
// Shared logical range work. The cursor borrows metadata and retains its
// reconstruction state; this function pairs requested logical blocks only.
// next() emits consecutive ordinals starting at first; count entries are
// admitted. Refilling may read predecessor metadata without emitting those
// blocks. Its boundary is a body-decode/content-consumer region, not a
// descriptor array.
template <class MetadataCursor, class Operations>
inline __attribute__((always_inline)) std::uint64_t
count_range(MetadataCursor &metadata, Operations &ops,
            const std::uint8_t *bodies, const std::uint8_t *query,
            unsigned first, unsigned count) {
  std::uint64_t matches = 0;
  unsigned ordinal = first;
#pragma clang loop unroll(disable)
  for (; count >= 2; count -= 2, ordinal += 2) {
    const auto a = metadata.next();
    const auto b = metadata.next();
    matches += ops.count_pair(bodies + entry_offset(a), entry_population(a),
                              bodies + entry_offset(b), entry_population(b),
                              query + 32 * ordinal);
  }
  if (count) {
    const auto a = metadata.next();
    matches += ops.count_one(bodies + entry_offset(a), entry_population(a),
                             query + 32 * ordinal);
  }
  return matches;
}
} // namespace ikea::heterogeneous
