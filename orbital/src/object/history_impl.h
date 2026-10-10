#pragma once
#include <orbital/detail/object/history.h>
namespace orbital::object_detail {
template<class Physical>
void History<Physical>::reserve(std::span<const Change> changes) {
  std::size_t new_debts = 0;
  for (const auto& change : changes) {
    const auto entry = entries.find(change.object);
    if ((!entry || !entries.indebted(entry)) && ((entry && head(entry)) || !change.representation)) ++new_debts;
  }
  entries.reserve_debt(new_debts);
}

template<class Physical>
void History<Physical>::prepare(std::span<const Change> changes, std::pmr::vector<Mutation>& output) {
  std::size_t count = 0;
  for (const auto& change : changes) {
    const auto entry = entries.find(change.object); count += entry && head(entry);
  }
  std::pmr::vector<Node> allocated(&memory);
  std::size_t initialized = 0;
  try {
    output.reserve(changes.size()); allocated = nodes.batch(count);
    for (const auto& change : changes) {
      const auto [handle, inserted] = entries.ensure(change.object); (void)inserted;
      const auto& entry = entries.get(handle);
      Node old = 0;
      if (entry.cut) {
        old = allocated[initialized];
        nodes.get(old).primary = entry.primary ? directory.add(entry.primary) : 0;
        nodes.get(old).cut = entry.cut; ++initialized;
        // This metadata borrows the pinned current tip until install. Its
        // older link is read at install because a drain may shorten history.
      }
      auto* next = change.representation.get();
      if (next) Physical::retain(next);
      output.push_back({handle, old, next});
    }
  } catch (...) {
    for (std::size_t i = 0; i < allocated.size(); ++i)
      if (i < initialized) erase_borrowed(allocated[i]); else nodes.erase(allocated[i]);
    for (auto& mutation : output) if (auto* value = std::exchange(mutation.replacement, nullptr)) Physical::release(value);
    for (const auto& change : changes) {
      const auto entry = entries.find(change.object);
      if (entry && !head(entry) && !entries.pinned(entry)) {
        const bool erased = entries.erase(entry); assert(erased); (void)erased;
      }
    }
    throw;
  }
  for (const auto& mutation : output) { const bool pinned = entries.pin(mutation.entry); assert(pinned); (void)pinned; }
}

template<class Physical>
bool History<Physical>::install(Mutation& mutation, std::uint64_t cut) noexcept {
  auto& entry = entries.get(mutation.entry);
  if (mutation.old) nodes.get(mutation.old).older = entry.older;
  entry.older = mutation.old; entry.cut = cut;
  entry.primary = std::exchange(mutation.replacement, nullptr);
  entries.unpin(mutation.entry); ++versions;
  return entry.older || !entry.primary;
}

template<class Physical>
void History<Physical>::abort(Mutation& mutation) noexcept {
  const auto handle = std::exchange(mutation.entry, ObjectHandle{});
  const auto old = std::exchange(mutation.old, Node{});
  auto* next = std::exchange(mutation.replacement, nullptr);
  assert(entries.pinned(handle)); entries.unpin(handle);
  if (!head(handle)) {
    entries.remove_debt(handle); const bool erased = entries.erase(handle); assert(erased); (void)erased;
  }
  if (old) erase_borrowed(old);
  if (next) Physical::release(next);
}

template<class Physical>
void History<Physical>::drop(Ref version) noexcept {
  const auto node = node_of(version); assert(node);
  auto* value = primary(version);
  auto* alternative = alternate(version);
  alternatives.erase(key(version)); erase_borrowed(node);
  if (alternative) Physical::release(alternative);
  if (value) Physical::release(value);
}

template<class Physical>
void History<Physical>::erase_head(ObjectHandle handle) noexcept {
  const auto current = head(handle);
  auto* value = current ? primary(current) : nullptr;
  auto* alternative = current ? alternate(current) : nullptr;
  if (current) alternatives.erase(key(current));
  const bool erased = entries.erase(handle); assert(erased); (void)erased;
  if (current) --versions;
  if (alternative) Physical::release(alternative);
  if (value) Physical::release(value);
}

template<class Physical>
orbital::Result<void> History<Physical>::adopt(Ref version, Value* next) {
  try {
    auto [where, inserted] = alternatives.try_emplace(key(version), next); Physical::retain(next);
    if (!inserted) { auto* old = where->second; where->second = next; Physical::release(old); }
  } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
  return {};
}

template<class Physical>
void History<Physical>::dispose() noexcept {
  entries.for_each_live([&](ObjectHandle handle, Entry& entry) {
    auto old = older(head(handle));
    while (old) { const auto next = older(old); drop(old); old = next; }
    auto* alternative = alternate(head(handle));
    alternatives.erase(key(head(handle)));
    auto* value = static_cast<Value*>(entry.primary);
    entry.primary = nullptr; entry.cut = 0; entry.older = 0;
    if (alternative) Physical::release(alternative);
    if (value) Physical::release(value);
  });
}
} // namespace orbital::object_detail
