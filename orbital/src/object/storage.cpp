#include <orbital/detail/object/directory.h>
#include <orbital/detail/object/versions.h>
namespace orbital::object_detail {
std::uint32_t Registry::add(void* value) {
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
std::uint32_t HistoryNodes::allocate(std::uint64_t cut) {
  if (!free_) {
    if (pages_.size() >= (std::numeric_limits<std::uint32_t>::max() - 1) / 64) throw std::bad_alloc();
    auto* page = make<Page>(memory_);
    try { pages_.push_back(page); }
    catch (...) { destroy(memory_, page); throw; }
    const auto base = static_cast<std::uint32_t>((pages_.size() - 1) * 64);
    for (std::uint32_t i = 0; i < 64; ++i) { page->nodes[i].older = free_; free_ = base + i + 1; }
  }
  auto result = free_;
  auto& n = get(result); free_ = n.older; n = {}; n.cut = cut;
  pages_[(result - 1) / 64]->occupied |= std::uint64_t{1} << ((result - 1) % 64);
  ++live_;
  return result;
}
HistoryNodes::~HistoryNodes() { assert(!live_); for (auto* page : pages_) destroy(memory_, page); }
auto HistoryNodes::batch(std::size_t count, std::uint64_t cut) -> std::pmr::vector<Handle> {
  std::pmr::vector<Handle> result(&memory_);
  result.reserve(count);
  try { for (std::size_t i = 0; i < count; ++i) result.push_back(allocate(cut)); }
  catch (...) { for (auto h : result) erase(h); throw; }
  return result;
}
void DebtQueue::reserve(std::size_t additional) {
  const auto free_count = slots_.size() - live_;
  if (additional <= free_count) return;
  const auto added = additional - free_count;
  if (added > maximum - slots_.size()) throw std::bad_alloc();
  const auto needed = slots_.size() + added;
  if (needed > slots_.capacity()) {
    const auto grown = std::min(maximum, std::max(needed, slots_.capacity() * 2));
    const auto copied = slots_.size();
    slots_.reserve(grown);
    reservation_work_ += copied; // Occasional capacity growth is still work.
  }
  while (slots_.size() < needed) {
    slots_.push_back({0, free_}); free_ = static_cast<std::uint32_t>(slots_.size());
    ++reservation_work_;
  }
}
void TipDirectory::rehash(std::size_t count) {
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

void TipDirectory::add_page() {
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

std::pair<Handle, bool> TipDirectory::ensure(ObjectId id) {
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

bool TipDirectory::pin(Handle handle) noexcept {
  if (!valid(handle) || (cell(handle).flags & pinned_bit)) return false;
  cell(handle).flags |= pinned_bit;
  return true;
}

bool TipDirectory::erase(Handle handle) noexcept {
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

TipDirectory::~TipDirectory() {
  for (auto* page : pages_) {
    std::destroy_at(page);
    resource().deallocate(page, sizeof(Page), alignof(Page));
  }
}

} // namespace orbital::object_detail
