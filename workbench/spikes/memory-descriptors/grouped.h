#pragma once

#include "arena.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace descriptors {

enum class grouped_layout {
  grid_major,
  class_major,
  class_major_reordered,
  class_aligned
};
const char *grouped_layout_name(grouped_layout layout);
inline constexpr std::array<std::size_t, 16> grouped_capacities{
    16,  32,  48,  64,   96,   128,  192,  256,
    384, 512, 768, 1024, 1536, 2048, 3072, 0};

struct grouped_geometry {
  grouped_layout kind{};
  std::size_t native_page{};
  std::size_t mapping_granule{};
  std::size_t trampolines_per_group{};
  std::size_t grids_per_group{};
  std::size_t payload_bytes{};
  std::size_t group_bytes{};
  std::array<std::size_t, 16> stride{};
  std::array<std::size_t, 16> class_offset{};
  std::array<std::size_t, 16> grid_step{};
};

grouped_geometry make_grouped_geometry(grouped_layout kind, std::size_t width,
                                       std::size_t native_page,
                                       std::size_t mapping_granule);

/// The owner admits live identities; callers retain byte use until release.
inline std::byte *grouped_resolve(Head head, const grouped_geometry &geometry) {
  const auto address = native(head);
  const auto coordinate = __atomic_load_n(
      reinterpret_cast<const std::uint8_t *>(address), __ATOMIC_ACQUIRE);
  const auto cls = coordinate & 15;
  assert(cls < 15);
  const auto grid = (address & (geometry.mapping_granule - 1)) >> 4;
  return reinterpret_cast<std::byte *>(
      (address & ~(geometry.mapping_granule - 1)) + geometry.mapping_granule +
      geometry.class_offset[cls] + grid * geometry.grid_step[cls] +
      (coordinate >> 4) * geometry.stride[cls]);
}

struct grouped_counters {
  std::size_t live_allocations{};
  std::size_t payload_granules{};
  std::size_t cached_empty_granules{};
  std::size_t map_calls{};
  std::size_t released_granules{};
  std::size_t allocations{};
  std::size_t releases{};
};
struct grouped_owner;

/// One serialized allocator owner; release requires all aliases and byte users
/// of that allocation to have ended. This candidate has no retained old homes,
/// relocation, logical versions, or concurrent allocation policy.
class grouped_arena {
public:
  grouped_arena(grouped_layout kind, std::size_t width, std::size_t groups,
                std::size_t mapping_granule = 0, unsigned id = 9);
  ~grouped_arena();
  grouped_arena(const grouped_arena &) = delete;
  grouped_arena &operator=(const grouped_arena &) = delete;
  const grouped_geometry &layout() const { return geometry_; }
  std::size_t grids() const {
    return groups_.size() * geometry_.grids_per_group;
  }
  std::size_t groups() const { return groups_.size(); }
  std::size_t reserved_bytes() const { return reserved_; }
  std::size_t control_granules() const { return groups_.size(); }
  std::size_t owner_record_bytes() const;
  grouped_counters counters() const;
  /// Returns uninitialized payload storage, or zero when all identities are
  /// full.
  Head allocate(std::size_t bytes);
  /// Constant-size bitmap work in an explicitly selected grid; zero if full.
  Head allocate_in_grid(std::size_t grid, std::size_t bytes);
  void release(Head head);
  /// Empty mapped capacity remains reusable until the caller chooses to trim
  /// it.
  std::size_t trim();
  std::size_t size(Head head) const;
  std::size_t capacity(Head head) const;

private:
  grouped_geometry geometry_;
  unsigned id_{};
  std::byte *base_{};
  std::size_t reserved_{};
  std::size_t next_grid_{};
  unsigned grid_shift_{};
  unsigned granule_shift_{};
  std::uint64_t group_reciprocal_{};
  std::vector<std::unique_ptr<grouped_owner>> groups_;
  grouped_counters counters_;
  struct head_location {
    grouped_owner *owner;
    std::size_t row;
    std::size_t grid;
    unsigned identity;
  };
  head_location locate(Head head) const;
  Head allocate_selected(std::size_t grid, unsigned cls, std::size_t bytes);
  void map_payload(grouped_owner &owner, std::size_t group, std::size_t first,
                   std::size_t last);
};

} // namespace descriptors
