#include "range_kernel.h"
namespace ikea::heterogeneous {
extern "C" std::uint64_t ikea_heterogeneous_local_inline(
    const std::uint8_t *metadata, unsigned capacity, const std::uint8_t *bodies,
    const std::uint8_t *query, unsigned first, unsigned count) noexcept {
  return execute_range<MetadataKind::local16, Execution::inlined>(
      metadata, capacity, bodies, query, first, count);
}
extern "C" std::uint64_t ikea_heterogeneous_local_split(
    const std::uint8_t *metadata, unsigned capacity, const std::uint8_t *bodies,
    const std::uint8_t *query, unsigned first, unsigned count) noexcept {
  return execute_range<MetadataKind::local16, Execution::split>(
      metadata, capacity, bodies, query, first, count);
}
} // namespace ikea::heterogeneous
