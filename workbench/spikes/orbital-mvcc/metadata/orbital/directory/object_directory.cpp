#include "object_directory.h"

#include <limits>
#include <memory>
#include <new>

namespace compact_directory {

std::uint64_t Directory::hash(ObjectId id) noexcept {
  return id.high ^ (id.low * 0x9e3779b97f4a7c15ULL);
}

std::size_t Directory::position(ObjectId id) const noexcept {
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

Handle Directory::find(ObjectId id) const noexcept {
  if (buckets_.empty()) return empty;
  const auto index = position(id);
  const auto handle = buckets_[index];
  return handle == empty || handle == deleted ? empty : handle;
}

void Directory::rehash(std::size_t count) {
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

void Directory::add_page() {
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
    page->entries[slot].head = free_;
    free_ = base + slot + 1;
  }
}

std::pair<Handle, bool> Directory::ensure(ObjectId id) {
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
  free_ = entry.head;
  entry = {id, 0, live};
  if (buckets_[index] == deleted) --tombstones_;
  buckets_[index] = handle;
  ++size_;
  return {handle, true};
}

bool Directory::pin(Handle handle) noexcept {
  if (!valid(handle) || (cell(handle).flags & pinned_bit)) return false;
  cell(handle).flags |= pinned_bit;
  return true;
}

bool Directory::erase(Handle handle) noexcept {
  if (!valid(handle)) return false;
  auto& entry = cell(handle);
  if (entry.flags & (pinned_bit | debt_bit)) return false;
  const auto index = position(entry.object);
  assert(buckets_[index] == handle);
  buckets_[index] = deleted;
  ++tombstones_; --size_;
  entry = {{}, free_, 0}; free_ = handle;
  return true;
}

Directory::~Directory() {
  for (auto* page : pages_) {
    std::destroy_at(page);
    resource().deallocate(page, sizeof(Page), alignof(Page));
  }
}

} // namespace compact_directory
