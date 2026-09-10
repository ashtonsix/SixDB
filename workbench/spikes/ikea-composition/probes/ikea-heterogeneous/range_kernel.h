#pragma once
#include "authoring.h"
#include "bec_region.h"
#include "metadata_native.h"
#include "source.h"
#include <cstring>

namespace ikea::heterogeneous {
template <MetadataKind Kind> class MetadataCursor {
  const std::uint8_t *metadata_;
  unsigned capacity_, next_, group_ = ~0u;
  EntryLanes16 entries_{};

public:
  MetadataCursor(const std::uint8_t *data, unsigned capacity, unsigned first)
      : metadata_(data), capacity_(capacity), next_(first) {}
  inline __attribute__((always_inline)) std::uint32_t next() {
    if constexpr (Kind == MetadataKind::direct32) {
      std::uint32_t raw;
      std::memcpy(&raw, metadata_ + 4 * next_++, 4);
      return ((raw >> 15) << 16) | (raw & 511);
    } else {
      const auto group = next_ & ~15u;
      if (group != group_) {
        entries_ = read_metadata16<Kind>(metadata_, capacity_, group);
        group_ = group;
      }
      return metadata_entry_at(entries_, next_++ % 16);
    }
  }
};
template <Execution Mode> struct CountOperations {
  inline __attribute__((always_inline)) std::uint64_t
  count_pair(const std::uint8_t *a, unsigned pa, const std::uint8_t *b,
             unsigned pb, const std::uint8_t *query) const {
    if constexpr (Mode == Execution::inlined)
      return bec_count2_inline(a, pa, b, pb, query);
    else
      return ikea_heterogeneous_bec_count2(a, pa, b, pb, query);
  }
  inline __attribute__((always_inline)) std::uint64_t
  count_one(const std::uint8_t *a, unsigned pa,
            const std::uint8_t *query) const {
    if constexpr (Mode == Execution::inlined)
      return bec_count1_inline(a, pa, query);
    else
      return ikea_heterogeneous_bec_count1(a, pa, query);
  }
};
template <MetadataKind Kind, Execution Mode>
inline __attribute__((always_inline)) std::uint64_t
execute_range(const std::uint8_t *metadata, unsigned capacity,
              const std::uint8_t *bodies, const std::uint8_t *query,
              unsigned first, unsigned count) noexcept {
  MetadataCursor<Kind> cursor(metadata, capacity, first);
  CountOperations<Mode> ops;
  return count_range(cursor, ops, bodies, query, first, count);
}
} // namespace ikea::heterogeneous
