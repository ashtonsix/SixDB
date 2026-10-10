#pragma once
#include "memory.h"
#include <array>
namespace orbital::object_detail {
struct Version { std::uint64_t cut{}; std::uint32_t older{}, primary{}; };
static_assert(sizeof(Version) == 16);
class HistoryNodes {
  struct Page { std::uint64_t occupied{}; std::array<Version, 64> nodes{}; };
  std::pmr::memory_resource& memory_;
  std::pmr::vector<Page*> pages_;
  std::uint32_t free_{};
  std::size_t live_{};
  std::uint32_t allocate();
public:
  using Node = Version;
  using Handle = std::uint32_t;
  explicit HistoryNodes(std::pmr::memory_resource& memory) : memory_(memory), pages_(&memory) {}
  ~HistoryNodes();
  Node& get(Handle h) const noexcept { return pages_[(h - 1) / 64]->nodes[(h - 1) % 64]; }
  std::uint64_t cut(Handle h) const noexcept { return get(h).cut; }
  std::pmr::vector<Handle> batch(std::size_t count);
  void erase(Handle h) noexcept {
    auto& page = *pages_[(h - 1) / 64];
    assert(page.occupied & (std::uint64_t{1} << ((h - 1) % 64)));
    page.occupied &= ~(std::uint64_t{1} << ((h - 1) % 64));
    get(h).older = free_; free_ = h; --live_;
  }
  std::size_t capacity_bytes() const noexcept { return pages_.size() * sizeof(Page) + pages_.capacity() * sizeof(Page*); }
};

} // namespace orbital::object_detail
