#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace orbital::object_detail {
template<class Physical> struct Access;
template<class T, class... Args> T* make(std::pmr::memory_resource& memory, Args&&... args) {
  auto* address = memory.allocate(sizeof(T), alignof(T));
  try { return std::construct_at(static_cast<T*>(address), std::forward<Args>(args)...); }
  catch (...) { memory.deallocate(address, sizeof(T), alignof(T)); throw; }
}
template<class T> void destroy(std::pmr::memory_resource& memory, T* value) noexcept {
  std::destroy_at(value); memory.deallocate(value, sizeof(T), alignof(T));
}
class Registry {
  union Slot { void* value; std::uint32_t next; Slot() : value(nullptr) {} };
  std::pmr::vector<Slot> slots_;
  std::uint32_t free_{};
public:
  explicit Registry(std::pmr::memory_resource& m) : slots_(&m) {}
  std::uint32_t add(void* value);
  void erase(std::uint32_t id) noexcept { slots_[id - 1].next = free_; free_ = id; }
  void* get(std::uint32_t id) const noexcept { return id ? slots_[id - 1].value : nullptr; }
  std::size_t capacity_bytes() const noexcept { return slots_.capacity() * sizeof(Slot); }
};

} // namespace orbital::object_detail
