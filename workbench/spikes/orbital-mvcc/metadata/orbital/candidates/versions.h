#pragma once

#include "common.h"
#include <array>
#include <type_traits>

namespace compact_version_probe {

struct Version32 {
  std::uint64_t cut{};
  void* primary{};
  void* alternative{};
  std::uint32_t older{};
};
struct Version16 { std::uint64_t cut{}; std::uint32_t older{}, primary{}; };
struct Version8 { std::uint32_t older{}, primary{}; };
static_assert(sizeof(Version32) == 32);
static_assert(sizeof(Version16) == 16 && sizeof(Version8) == 8);

template<class T> class Slab {
  struct Page { std::uint64_t occupied{}; std::array<T, 64> nodes{}; };
  Memory& memory_;
  std::pmr::vector<Page*> pages_;
  std::uint32_t free_{};
  std::size_t live_{};
  std::uint32_t allocate(std::uint64_t cut) {
    if (!free_) {
      if (pages_.size() >= (std::numeric_limits<std::uint32_t>::max() - 1) / 64) throw std::bad_alloc();
      auto* page = memory_.make<Page>();
      try { pages_.push_back(page); }
      catch (...) { memory_.destroy(page); throw; }
      const auto base = static_cast<std::uint32_t>((pages_.size() - 1) * 64);
      for (std::uint32_t i = 0; i < 64; ++i) { page->nodes[i].older = free_; free_ = base + i + 1; }
    }
    auto result = free_;
    auto& n = get(result); free_ = n.older; n = {}; n.cut = cut;
    pages_[(result - 1) / 64]->occupied |= std::uint64_t{1} << ((result - 1) % 64);
    ++live_;
    return result;
  }
public:
  using Node = T;
  using Handle = std::uint32_t;
  static constexpr bool indexed_rep = std::is_same_v<T, Version16>;
  static constexpr bool inline_alternative = std::is_same_v<T, Version32>;
  explicit Slab(Memory& memory) : memory_(memory), pages_(&memory) {}
  ~Slab() { assert(!live_); for (auto* page : pages_) memory_.destroy(page); }
  Node& get(Handle h) const noexcept { return pages_[(h - 1) / 64]->nodes[(h - 1) % 64]; }
  std::uint64_t cut(Handle h) const noexcept { return get(h).cut; }
  std::pmr::vector<Handle> batch(std::size_t count, std::uint64_t cut) {
    std::pmr::vector<Handle> result(&memory_);
    result.reserve(count);
    try { for (std::size_t i = 0; i < count; ++i) result.push_back(allocate(cut)); }
    catch (...) { for (auto h : result) erase(h); throw; }
    return result;
  }
  void erase(Handle h) noexcept {
    auto& page = *pages_[(h - 1) / 64];
    assert(page.occupied & (std::uint64_t{1} << ((h - 1) % 64)));
    page.occupied &= ~(std::uint64_t{1} << ((h - 1) % 64));
    get(h).older = free_; free_ = h; --live_;
  }
  std::size_t capacity_bytes() const noexcept { return pages_.size() * sizeof(Page) + pages_.capacity() * sizeof(Page*); }
};

// A cut belongs to a publication chunk. Nodes hold no hidden per-node cut or
// lifetime header. A chunk cannot be reclaimed while any one of its nodes lives.
class Chunks8 {
  struct Chunk { std::uint64_t cut; std::uint32_t count, live; };
  union Slot { Chunk* chunk; std::uint32_t next; Slot() : chunk(nullptr) {} };
  Memory& memory_;
  std::pmr::vector<Slot> chunks_;
  std::uint32_t free_{};
  std::size_t live_{};
  std::size_t allocated_bytes_{};
  static Version8* nodes(Chunk* chunk) noexcept {
    return static_cast<Version8*>(static_cast<void*>(static_cast<std::byte*>(static_cast<void*>(chunk)) + sizeof(Chunk)));
  }
  void discard(std::uint32_t id, Chunk* chunk) noexcept {
    const auto bytes = sizeof(Chunk) + sizeof(Version8) * chunk->count;
    allocated_bytes_ -= bytes;
    std::destroy_at(chunk);
    memory_.deallocate(chunk, bytes, alignof(Chunk));
    chunks_[id - 1].next = free_; free_ = id;
  }
public:
  using Node = Version8;
  using Handle = std::uint32_t;
  static constexpr bool indexed_rep = true;
  static constexpr bool inline_alternative = false;
  explicit Chunks8(Memory& memory) : memory_(memory), chunks_(&memory) {}
  ~Chunks8() { assert(!live_); }
  Node& get(Handle h) const noexcept { return nodes(chunks_[(h >> 8) - 1].chunk)[h & 255]; }
  std::uint64_t cut(Handle h) const noexcept { return chunks_[(h >> 8) - 1].chunk->cut; }
  std::pmr::vector<Handle> batch(std::size_t count, std::uint64_t cut) {
    std::pmr::vector<Handle> result(&memory_); result.reserve(count);
    try {
      while (count) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(count, 256));
        const auto bytes = sizeof(Chunk) + sizeof(Version8) * n;
        auto* chunk = static_cast<Chunk*>(memory_.allocate(bytes, alignof(Chunk)));
        std::construct_at(chunk, Chunk{cut, n, n});
        std::uint32_t id;
        if (free_) { id = free_; free_ = chunks_[id - 1].next; }
        else {
          if (chunks_.size() == 0xffffff) { memory_.deallocate(chunk, bytes, alignof(Chunk)); throw std::bad_alloc(); }
          try { chunks_.emplace_back(); }
          catch (...) { memory_.deallocate(chunk, bytes, alignof(Chunk)); throw; }
          id = static_cast<std::uint32_t>(chunks_.size());
        }
        chunks_[id - 1].chunk = chunk;
        allocated_bytes_ += bytes;
        for (std::uint32_t i = 0; i < n; ++i) {
          std::construct_at(nodes(chunk) + i); result.push_back((id << 8) | i);
        }
        live_ += n; count -= n;
      }
    } catch (...) { for (auto h : result) erase(h); throw; }
    return result;
  }
  void erase(Handle h) noexcept {
    const auto id = h >> 8;
    auto* chunk = chunks_[id - 1].chunk;
    assert(chunk->live); --chunk->live; --live_;
    get(h) = {};
    if (!chunk->live) discard(id, chunk);
  }
  std::size_t capacity_bytes() const noexcept { return allocated_bytes_ + chunks_.capacity() * sizeof(Slot); }
};

} // namespace compact_version_probe
