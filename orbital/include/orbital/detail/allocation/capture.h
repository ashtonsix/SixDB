#pragma once
#include <orbital/allocation.h>

namespace orbital::allocation::detail {
// Object capture validates every source after metadata callbacks, then transfers uses
// without calls that can refuse or change another source.
Result<void> ready_for_capture(const Allocation&) noexcept;
inline Storage* take_for_capture(Allocation& allocation) noexcept {
  auto* storage = std::exchange(allocation.storage, nullptr);
  allocation.address = nullptr;
  allocation.size = allocation.readable_padding = 0;
  return storage;
}
} // namespace orbital::allocation::detail
