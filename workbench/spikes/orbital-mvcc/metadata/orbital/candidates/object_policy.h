#pragma once

#include "common.h"
#include "physical.h"
#include "debt_queue.h"
#include "../directory/object_directory.h"
#include <unordered_map>

// Directory policies for the same embedded history implementation. Stable
// handles remain private; neither a handle nor a debt index is Object identity.
namespace compact_history {

struct ObjectHash {
  std::size_t operator()(orbital::ObjectId id) const noexcept {
    return id.high ^ (id.low * UINT64_C(0x9e3779b97f4a7c15));
  }
};

class HashObjects {
public:
  struct Entry {
    const orbital::ObjectId* identity{};
    std::uint32_t head{}, prepared{};
    Entry* previous{};
    Entry* next{};
    bool debt{};
  };
  using Handle = Entry*;
private:
  std::pmr::unordered_map<orbital::ObjectId, Entry, ObjectHash> entries_;
  Entry* first_debt_{};
  Entry* last_debt_{};
  std::size_t debt_count_{};
public:
  explicit HashObjects(compact_version_probe::Memory& memory) : entries_(&memory) {}
  Handle find(orbital::ObjectId id) noexcept {
    auto at = entries_.find(id);
    return at == entries_.end() ? nullptr : &at->second;
  }
  std::pair<Handle, bool> ensure(orbital::ObjectId id) {
    auto [where, inserted] = entries_.try_emplace(id);
    if (inserted) where->second.identity = &where->first;
    return {&where->second, inserted};
  }
  Entry& get(Handle entry) noexcept { assert(entry); return *entry; }
  const Entry& get(Handle entry) const noexcept { assert(entry); return *entry; }
  orbital::ObjectId object(Handle entry) const noexcept { return *entry->identity; }
  bool pinned(Handle entry) const noexcept { return entry->prepared; }
  bool indebted(Handle entry) const noexcept { return entry->debt; }
  bool pin(Handle entry) noexcept {
    if (entry->prepared == UINT32_MAX) return false;
    ++entry->prepared; return true;
  }
  void unpin(Handle entry) noexcept { assert(entry->prepared); --entry->prepared; }
  bool erase(Handle entry) noexcept {
    if (pinned(entry) || entry->debt) return false;
    return entries_.erase(*entry->identity) != 0;
  }
  void reserve_debt(std::size_t) {}
  void add_debt(Handle entry) noexcept {
    if (entry->debt) return;
    entry->previous = last_debt_; entry->next = nullptr;
    if (last_debt_) last_debt_->next = entry; else first_debt_ = entry;
    last_debt_ = entry; entry->debt = true; ++debt_count_;
  }
  void remove_debt(Handle entry) noexcept {
    if (!entry->debt) return;
    if (entry->previous) entry->previous->next = entry->next; else first_debt_ = entry->next;
    if (entry->next) entry->next->previous = entry->previous; else last_debt_ = entry->previous;
    entry->previous = entry->next = nullptr; entry->debt = false; --debt_count_;
  }
  Handle front_debt() const noexcept { return first_debt_; }
  void rotate_debt() noexcept {
    if (!first_debt_ || first_debt_ == last_debt_) return;
    auto* entry = first_debt_; first_debt_ = entry->next; first_debt_->previous = nullptr;
    entry->next = nullptr; entry->previous = last_debt_; last_debt_->next = entry; last_debt_ = entry;
  }
  std::size_t debt_count() const noexcept { return debt_count_; }
  std::size_t debt_reservation_work() const noexcept { return 0; }
  template<class Visitor> void for_each_live(Visitor&& visit) {
    for (auto& [id, entry] : entries_) { (void)id; visit(&entry, entry); }
  }
  std::size_t size() const noexcept { return entries_.size(); }
  // Whole PMR allocation accounting includes implementation-specific hash-node
  // headers. No guessed sizeof(node) is reported as an exact capacity here.
  std::size_t capacity_bytes() const noexcept { return 0; }
  std::size_t debt_capacity_bytes() const noexcept { return 0; }
};

class CompactObjects {
public:
  using Entry = compact_directory::Entry;
  using Handle = compact_directory::Handle;
private:
  static constexpr std::uint32_t flag_mask = 7;
  static constexpr std::size_t max_debts = (UINT32_MAX >> 3);
  compact_directory::Directory entries_;
  detail::DebtQueue debts_;
  void encode_index(Handle entry, std::size_t index) noexcept {
    assert(index < max_debts);
    auto& flags = entries_.get(entry).flags;
    flags = (flags & flag_mask) | static_cast<std::uint32_t>((index + 1) << 3);
  }
public:
  explicit CompactObjects(compact_version_probe::Memory& memory) : entries_(memory), debts_(memory) {}
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

  void reserve_debt(std::size_t additional) { debts_.reserve(additional); }
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
  template<class Visitor> void for_each_live(Visitor&& visit) { entries_.for_each_live(visit); }
  std::size_t size() const noexcept { return entries_.size(); }
  std::size_t capacity_bytes() const noexcept { return entries_.capacity_bytes(); }
  std::size_t debt_capacity_bytes() const noexcept { return debts_.capacity_bytes(); }
};

static_assert(sizeof(HashObjects::Entry) == 40);
static_assert(sizeof(CompactObjects::Entry) == 24);
static_assert(sizeof(CompactObjects::Handle) == 4);

} // namespace compact_history
