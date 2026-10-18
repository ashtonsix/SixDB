#pragma once
#include <orbital/detail/object/representation.h>
#include <orbital/detail/allocation/capture.h>
namespace orbital::object_detail {
bool valid(Bytes, std::size_t padding = 0);
inline Result<void> capture_ready(allocation::Allocation* source) noexcept {
  return source && *source ? allocation::detail::ready_for_capture(*source) : Result<void>{};
}
inline allocation::Storage* take_source(allocation::Allocation* source) noexcept {
  return source && *source ? allocation::detail::take_for_capture(*source) : nullptr;
}
void drop(allocation::Storage*);
Result<allocation::Storage*> pin(allocation::Storage*);
bool within(const allocation::Allocation&, Bytes, std::uint32_t padding);
bool unique_allocations(std::span<const PartSource>, std::span<allocation::Storage*> scratch);
} // namespace orbital::object_detail
