#include <loom/detail/ring_storage.h>
#include <loom/worker.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace loom {
namespace detail {
orbital::Result<RingStorage> ring_storage(Worker& worker, std::size_t active,
    std::size_t overflow, std::size_t slot_bytes, std::size_t alignment) {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  if (!active || active > maximum - overflow || !slot_bytes || !std::has_single_bit(alignment))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto count = active + overflow;
  RingLayout layout;
  if (count > maximum / slot_bytes) return std::unexpected(orbital::Error::invalid_argument);
  layout.bytes = count * slot_bytes;
  const auto append = [&](std::size_t n, std::size_t size, std::size_t& offset) {
    constexpr auto align = alignof(std::size_t);
    if (layout.bytes > maximum - (align - 1)) return false;
    layout.bytes = (layout.bytes + align - 1) & ~(align - 1);
    offset = layout.bytes;
    if (n > (maximum - layout.bytes) / size) return false;
    layout.bytes += n * size;
    return true;
  };
  static_assert(alignof(void*) <= alignof(std::size_t));
  if (!append(count, sizeof(std::size_t), layout.free) ||
      !append(overflow, sizeof(void*), layout.overflow))
    return std::unexpected(orbital::Error::invalid_argument);
  const orbital::allocation::AllocationRequest request{
    .bytes = layout.bytes, .alignment = std::max(std::size_t(64), alignment)};
  auto allocation = loom::allocate(worker, request);
  if (!allocation) return std::unexpected(allocation.error());
  return RingStorage{std::move(*allocation), layout};
}

} // namespace detail
} // namespace loom
