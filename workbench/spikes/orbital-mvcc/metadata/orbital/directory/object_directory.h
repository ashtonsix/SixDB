#pragma once

#include <orbital/types.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <utility>
#include <vector>

// Single-owner embedded directory candidate. IDs remain full-width; stable
// entry handles are internal locations, not identities or exported lifetimes.
// The resource outlives Directory. It owns no version or physical backing.
namespace compact_directory {

using Handle = std::uint32_t;
using orbital::ObjectId;

struct Entry {
  ObjectId object{};
  std::uint32_t head{};
  // Low three bits belong to Directory. The owner may use higher bits for
  // cold debt indexing; ordinary pin/debt operations preserve them. Erase and
  // reuse clear every bit, so remove the owner's debt slot before erasure.
  std::uint32_t flags{};
};
static_assert(sizeof(Entry) == 24);

class Directory {
  static constexpr Handle empty = 0, deleted = UINT32_MAX;
  static constexpr std::uint32_t live = 1, pinned_bit = 2, debt_bit = 4;
  static constexpr std::uint32_t page_entries = 32;
  struct Page { std::array<Entry, page_entries> entries; };
  std::pmr::vector<Page*> pages_;
  std::pmr::vector<Handle> buckets_;
  std::size_t size_{}, tombstones_{};
  Handle free_{};

  static std::uint64_t hash(ObjectId) noexcept;
  std::size_t position(ObjectId) const noexcept;
  void rehash(std::size_t);
  void add_page();
  std::pmr::memory_resource& resource() noexcept { return *pages_.get_allocator().resource(); }
  Entry& cell(Handle handle) const noexcept {
    return pages_[(handle - 1) / page_entries]->entries[(handle - 1) % page_entries];
  }
public:
  explicit Directory(std::pmr::memory_resource& resource) : pages_(&resource), buckets_(&resource) {}
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;
  ~Directory();

  Handle find(ObjectId) const noexcept;
  // Refusal preserves existing bindings and pins; capacity may have grown.
  std::pair<Handle, bool> ensure(ObjectId);
  Entry& get(Handle handle) noexcept { assert(valid(handle)); return cell(handle); }
  const Entry& get(Handle handle) const noexcept { assert(valid(handle)); return cell(handle); }
  bool valid(Handle handle) const noexcept {
    return handle && handle != deleted && (handle - 1) / page_entries < pages_.size() &&
        (cell(handle).flags & live);
  }

  // Owner first removes its cold debt record and every version/backing use.
  // Erasure refuses an accepted-work pin or a still-enrolled debt record.
  bool erase(Handle) noexcept;
  bool pin(Handle) noexcept;
  void unpin(Handle handle) noexcept { assert(pinned(handle)); cell(handle).flags &= ~pinned_bit; }
  bool pinned(Handle handle) const noexcept { return valid(handle) && (cell(handle).flags & pinned_bit); }
  bool indebted(Handle handle) const noexcept { return valid(handle) && (cell(handle).flags & debt_bit); }
  void set_indebted(Handle handle, bool value) noexcept {
    assert(valid(handle));
    if (value) cell(handle).flags |= debt_bit; else cell(handle).flags &= ~debt_bit;
  }

  // Cold iteration visits capacity and skips free cells. Visitor must not mutate
  // the directory; callers that invoke owner callbacks stop/restart separately.
  template<class Visitor> void for_each_live(Visitor&& visit) {
    for (std::size_t page = 0; page < pages_.size(); ++page)
      for (std::uint32_t slot = 0; slot < page_entries; ++slot) {
        auto& entry = pages_[page]->entries[slot];
        if (entry.flags & live) visit(static_cast<Handle>(page * page_entries + slot + 1), entry);
      }
  }

  std::size_t size() const noexcept { return size_; }
  std::size_t tombstones() const noexcept { return tombstones_; }
  std::size_t bucket_bytes() const noexcept { return buckets_.capacity() * sizeof(Handle); }
  std::size_t entry_bytes() const noexcept { return pages_.size() * sizeof(Page); }
  std::size_t page_pointer_bytes() const noexcept { return pages_.capacity() * sizeof(Page*); }
  std::size_t capacity_bytes() const noexcept { return bucket_bytes() + entry_bytes() + page_pointer_bytes(); }
};

} // namespace compact_directory
