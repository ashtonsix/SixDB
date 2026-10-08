#pragma once

// Private fixed bank layout for typed Rings. Offsets are bytes from the start
// of the Allocation; constructing and retiring slots belongs to the Ring.

#include <orbital/allocation.h>
#include <cstddef>

namespace loom {
struct Worker;
namespace detail {

struct RingLayout {
  std::size_t bytes{}, free{}, overflow{};
};

struct RingStorage {
  // Private fixed bank: no growth or release before owner close after drain.
  orbital::allocation::Allocation allocation;
  RingLayout layout;
};

[[nodiscard]] orbital::Result<RingStorage> ring_storage(Worker&,
    std::size_t active, std::size_t overflow, std::size_t slot_bytes,
    std::size_t slot_alignment);

} // namespace detail
} // namespace loom
