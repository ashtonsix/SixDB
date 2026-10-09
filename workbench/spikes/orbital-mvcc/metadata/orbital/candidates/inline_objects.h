#pragma once

#include "common.h"
#include "debt_queue.h"

#include <orbital/types.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <utility>
#include <vector>

// Inline-current-version directory candidate. The 40-byte record owns the
// current cut/physical pointer; only older versions need separate history slots. IDs remain full-width; stable
// entry handles are internal locations, not identities or exported lifetimes.
// The resource outlives TipDirectory. Its caller owns physical lifetimes and every old-history slot.
namespace compact_history::inline_detail {

using Handle = std::uint32_t;
using orbital::ObjectId;

struct Entry {
  ObjectId object{};
  std::uint64_t cut{};
  void* primary{};
  std::uint32_t older{};
  // Low three bits belong to TipDirectory. The owner may use higher bits for
  // cold debt indexing; ordinary pin/debt operations preserve them. Erase and
  // reuse clear every bit, so remove the owner's debt slot before erasure.
  std::uint32_t flags{};
};
static_assert(sizeof(Entry) == 40);

class TipDirectory {
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
  explicit TipDirectory(std::pmr::memory_resource& resource) : pages_(&resource), buckets_(&resource) {}
  TipDirectory(const TipDirectory&) = delete;
  TipDirectory& operator=(const TipDirectory&) = delete;
  ~TipDirectory();

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

} // namespace compact_history::inline_detail

namespace compact_history::inline_detail {

inline std::uint64_t TipDirectory::hash(ObjectId id) noexcept {
  return id.high ^ (id.low * 0x9e3779b97f4a7c15ULL);
}

inline std::size_t TipDirectory::position(ObjectId id) const noexcept {
  assert(!buckets_.empty());
  const auto mask = buckets_.size() - 1;
  auto index = static_cast<std::size_t>(hash(id)) & mask;
  auto first_deleted = buckets_.size();
  for (std::size_t scanned = 0; scanned < buckets_.size(); ++scanned) {
    const auto handle = buckets_[index];
    if (handle == empty) return first_deleted == buckets_.size() ? index : first_deleted;
    if (handle == deleted) {
      if (first_deleted == buckets_.size()) first_deleted = index;
    } else if (cell(handle).object == id) return index;
    index = (index + 1) & mask;
  }
  assert(first_deleted != buckets_.size());
  return first_deleted;
}

inline Handle TipDirectory::find(ObjectId id) const noexcept {
  if (buckets_.empty()) return empty;
  const auto index = position(id);
  const auto handle = buckets_[index];
  return handle == empty || handle == deleted ? empty : handle;
}

inline void TipDirectory::rehash(std::size_t count) {
  if (count > buckets_.max_size()) throw std::bad_alloc();
  std::pmr::vector<Handle> next(count, empty, &resource());
  const auto mask = count - 1;
  for (auto handle : buckets_) {
    if (handle == empty || handle == deleted) continue;
    auto index = static_cast<std::size_t>(hash(cell(handle).object)) & mask;
    while (next[index] != empty) index = (index + 1) & mask;
    next[index] = handle;
  }
  buckets_.swap(next);
  tombstones_ = 0;
}

inline void TipDirectory::add_page() {
  if (pages_.size() >= (std::numeric_limits<Handle>::max() - 1) / page_entries)
    throw std::bad_alloc();
  auto* page = std::construct_at(static_cast<Page*>(resource().allocate(sizeof(Page), alignof(Page))));
  try { pages_.push_back(page); }
  catch (...) {
    std::destroy_at(page);
    resource().deallocate(page, sizeof(Page), alignof(Page));
    throw;
  }
  const auto base = static_cast<Handle>((pages_.size() - 1) * page_entries);
  for (Handle slot = 0; slot < page_entries; ++slot) {
    page->entries[slot].older = free_;
    free_ = base + slot + 1;
  }
}

inline std::pair<Handle, bool> TipDirectory::ensure(ObjectId id) {
  if (auto found = find(id)) return {found, false};
  const auto threshold = buckets_.size() - buckets_.size() / 4;
  if (buckets_.empty()) rehash(16);
  else if (size_ + tombstones_ >= threshold) {
    if (size_ < threshold) rehash(buckets_.size());
    else {
      if (buckets_.size() > buckets_.max_size() / 2) throw std::bad_alloc();
      rehash(buckets_.size() * 2);
    }
  }
  if (!free_) add_page();
  const auto index = position(id);
  const auto handle = free_;
  auto& entry = cell(handle);
  free_ = entry.older;
  entry = {id, 0, nullptr, 0, live};
  if (buckets_[index] == deleted) --tombstones_;
  buckets_[index] = handle;
  ++size_;
  return {handle, true};
}

inline bool TipDirectory::pin(Handle handle) noexcept {
  if (!valid(handle) || (cell(handle).flags & pinned_bit)) return false;
  cell(handle).flags |= pinned_bit;
  return true;
}

inline bool TipDirectory::erase(Handle handle) noexcept {
  if (!valid(handle)) return false;
  auto& entry = cell(handle);
  if (entry.flags & (pinned_bit | debt_bit)) return false;
  const auto index = position(entry.object);
  assert(buckets_[index] == handle);
  buckets_[index] = deleted;
  ++tombstones_; --size_;
  entry = {{}, 0, nullptr, free_, 0}; free_ = handle;
  return true;
}

inline TipDirectory::~TipDirectory() {
  for (auto* page : pages_) {
    std::destroy_at(page);
    resource().deallocate(page, sizeof(Page), alignof(Page));
  }
}

} // namespace compact_history::inline_detail

namespace compact_history {

class TipObjects {
public:
  static constexpr bool inline_current = true;
  using Entry = inline_detail::Entry;
  using Handle = inline_detail::Handle;
private:
  static constexpr std::uint32_t flag_mask = 7;
  static constexpr std::size_t max_debts = (UINT32_MAX >> 3);
  inline_detail::TipDirectory entries_;
  detail::DebtQueue debts_;
  void encode_index(Handle entry, std::size_t index) noexcept {
    assert(index < max_debts);
    auto& flags = entries_.get(entry).flags;
    flags = (flags & flag_mask) | static_cast<std::uint32_t>((index + 1) << 3);
  }
public:
  explicit TipObjects(compact_version_probe::Memory& memory) : entries_(memory), debts_(memory) {}
  Handle find(orbital::ObjectId id) const noexcept { return entries_.find(id); }
  std::pair<Handle, bool> ensure(orbital::ObjectId id) { return entries_.ensure(id); }
  Entry& get(Handle entry) noexcept { return entries_.get(entry); }
  const Entry& get(Handle entry) const noexcept { return entries_.get(entry); }
  orbital::ObjectId object(Handle entry) const noexcept { return entries_.get(entry).object; }
  bool pinned(Handle entry) const noexcept { return entries_.pinned(entry); }
  bool indebted(Handle entry) const noexcept { return entries_.indebted(entry); }
  bool pin(Handle entry) noexcept { return entries_.pin(entry); }
  void unpin(Handle entry) noexcept { entries_.unpin(entry); }
  bool erase(Handle entry) noexcept { return entries_.erase(entry); }

  bool reserve_debt(std::size_t additional) {
    debts_.reserve(additional); return false;
  }
  void add_debt(Handle entry) noexcept {
    if (entries_.indebted(entry)) return;
    encode_index(entry, debts_.add(entry)); entries_.set_indebted(entry, true);
  }
  void remove_debt(Handle entry) noexcept {
    if (!entries_.indebted(entry)) return;
    auto& flags = entries_.get(entry).flags;
    const auto index = (flags >> 3) - 1;
    debts_.remove(index); flags &= flag_mask; entries_.set_indebted(entry, false);
  }
  Handle front_debt() const noexcept { return debts_.front(); }
  void rotate_debt() noexcept { debts_.rotate(); }
  std::size_t debt_count() const noexcept { return debts_.size(); }
  std::size_t debt_reservation_work() const noexcept { return debts_.reservation_work(); }
  template<class Visitor> void for_each_debt(Visitor&& visit) {
    debts_.for_each([&](Handle entry) { return visit(entry, entries_.get(entry)); });
  }
  template<class Visitor> void for_each_live(Visitor&& visit) { entries_.for_each_live(visit); }
  std::size_t size() const noexcept { return entries_.size(); }
  std::size_t capacity_bytes() const noexcept { return entries_.capacity_bytes(); }
  std::size_t debt_capacity_bytes() const noexcept { return debts_.capacity_bytes(); }
};

static_assert(sizeof(TipObjects::Entry) == 40);

} // namespace compact_history
