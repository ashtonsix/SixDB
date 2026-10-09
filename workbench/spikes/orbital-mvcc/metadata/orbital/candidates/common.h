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

namespace compact_version_probe {

struct Memory final : std::pmr::memory_resource {
  std::size_t live{}, peak{}, calls{}, limit = std::numeric_limits<std::size_t>::max();
  Memory() = default;
  Memory(const Memory&) = delete;
  Memory& operator=(const Memory&) = delete;
  Memory(Memory&&) = delete;
  Memory& operator=(Memory&&) = delete;
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (live > limit || bytes > limit - live) throw std::bad_alloc();
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes;
    peak = std::max(peak, live);
    ++calls;
    return result;
  }
  void do_deallocate(void* address, std::size_t bytes, std::size_t alignment) override {
    assert(live >= bytes);
    live -= bytes;
    std::pmr::new_delete_resource()->deallocate(address, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
  template<class T, class... Args> T* make(Args&&... args) {
    auto* address = allocate(sizeof(T), alignof(T));
    try { return std::construct_at(static_cast<T*>(address), std::forward<Args>(args)...); }
    catch (...) { deallocate(address, sizeof(T), alignof(T)); throw; }
  }
  template<class T> void destroy(T* value) noexcept {
    std::destroy_at(value);
    deallocate(value, sizeof(T), alignof(T));
  }
};

class Registry {
  union Slot { void* value; std::uint32_t next; Slot() : value(nullptr) {} };
  std::pmr::vector<Slot> slots_;
  std::uint32_t free_{};
public:
  explicit Registry(Memory& m) : slots_(&m) {}
  std::uint32_t add(void* value) {
    std::uint32_t id;
    if (free_) { id = free_; free_ = slots_[id - 1].next; }
    else {
      if (slots_.size() == std::numeric_limits<std::uint32_t>::max()) throw std::bad_alloc();
      slots_.emplace_back();
      id = static_cast<std::uint32_t>(slots_.size());
    }
    slots_[id - 1].value = value;
    return id;
  }
  void erase(std::uint32_t id) noexcept { slots_[id - 1].next = free_; free_ = id; }
  void* get(std::uint32_t id) const noexcept { return id ? slots_[id - 1].value : nullptr; }
  std::size_t capacity_bytes() const noexcept { return slots_.capacity() * sizeof(Slot); }
};

} // namespace compact_version_probe
