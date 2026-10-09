#pragma once

#include "history.h"
#include "inline_objects.h"

namespace compact_history {

// One inline-tip alternative, using 16-byte historical nodes. Ref is a private
// {entry handle, older-node handle}; node zero denotes that entry's current tip.
// It never escapes a protected Core operation. Public Selection remains 8 bytes.
template<class Nodes, class Physical, class Objects> struct InlineHistory {
  static_assert(std::is_same_v<typename Nodes::Node, compact_version_probe::Version16>);
  using Value = typename Physical::Value;
  using Selection = typename Physical::Selection;
  using Change = ChangeOf<PhysicalOwner<Physical>>;
  using Entry = typename Objects::Entry;
  using ObjectHandle = typename Objects::Handle;
  using Node = typename Nodes::Handle;
  using Ref = std::uint64_t;
  struct Mutation { ObjectHandle entry; Node old; Value* replacement; };
  struct AlternativeKey {
    ObjectHandle entry;
    std::uint64_t cut;
    bool operator==(const AlternativeKey&) const = default;
  };
  struct AlternativeHash {
    std::size_t operator()(AlternativeKey key) const noexcept {
      return key.cut ^ (std::uint64_t(key.entry) * UINT64_C(0x9e3779b97f4a7c15));
    }
  };

  compact_version_probe::Memory& memory;
  Nodes nodes;
  compact_version_probe::Registry directory;
  Objects entries;
  std::pmr::unordered_map<AlternativeKey, Value*, AlternativeHash> alternatives;
  std::size_t versions{};

  explicit InlineHistory(compact_version_probe::Memory& resource)
      : memory(resource), nodes(resource), directory(resource), entries(resource), alternatives(&resource) {}
  static ObjectHandle entry_of(Ref ref) noexcept { return static_cast<ObjectHandle>(ref >> 32); }
  static Node node_of(Ref ref) noexcept { return static_cast<Node>(ref); }
  static Ref ref(ObjectHandle entry, Node node = 0) noexcept { return (Ref(entry) << 32) | node; }
  Ref head(ObjectHandle entry) const noexcept { return entries.get(entry).cut ? ref(entry) : 0; }
  Ref older(Ref version) const noexcept {
    const auto node = node_of(version);
    const auto old = node ? nodes.get(node).older : entries.get(entry_of(version)).older;
    return old ? ref(entry_of(version), old) : 0;
  }
  void link_older(Ref version, Ref old) noexcept {
    assert(!old || entry_of(version) == entry_of(old));
    const auto node = node_of(version);
    if (node) nodes.get(node).older = node_of(old);
    else entries.get(entry_of(version)).older = node_of(old);
  }
  std::uint64_t version_cut(Ref version) const noexcept {
    const auto node = node_of(version);
    return node ? nodes.cut(node) : entries.get(entry_of(version)).cut;
  }
  Value* primary(Ref version) const noexcept {
    const auto node = node_of(version);
    return static_cast<Value*>(node ? directory.get(nodes.get(node).primary) : entries.get(entry_of(version)).primary);
  }
  AlternativeKey key(Ref version) const noexcept { return {entry_of(version), version_cut(version)}; }
  Value* alternate(Ref version) const noexcept {
    auto at = alternatives.find(key(version)); return at == alternatives.end() ? nullptr : at->second;
  }
  Ref locate(std::uint64_t cut, orbital::ObjectId id) noexcept {
    const auto entry = entries.find(id);
    auto version = entry ? head(entry) : Ref{};
    while (version && version_cut(version) > cut) version = older(version);
    return version;
  }
  Selection resolve(std::uint64_t cut, orbital::ObjectId id, bool alternative) noexcept {
    const auto handle = entries.find(id);
    if (!handle) return {};
    const auto& entry = entries.get(handle);
    if (entry.cut && entry.cut <= cut)
      return Physical::selection(alternative ? alternate(ref(handle)) : static_cast<Value*>(entry.primary));
    auto version = entry.older ? ref(handle, entry.older) : Ref{};
    while (version && version_cut(version) > cut) version = older(version);
    if (!version || !primary(version)) return {};
    return Physical::selection(alternative ? alternate(version) : primary(version));
  }
  bool reserve(std::span<const Change> changes) {
    std::size_t new_debts = 0;
    for (const auto& change : changes) {
      const auto entry = entries.find(change.object);
      if ((!entry || !entries.indebted(entry)) && ((entry && head(entry)) || !change.representation)) ++new_debts;
    }
    return entries.reserve_debt(new_debts);
  }
  void erase_borrowed(Node node) noexcept {
    if (nodes.get(node).primary) directory.erase(nodes.get(node).primary);
    nodes.erase(node);
  }
  void prepare(std::span<const Change> changes, std::uint64_t, std::pmr::vector<Mutation>& output) {
    std::size_t count = 0;
    for (const auto& change : changes) {
      const auto entry = entries.find(change.object); count += entry && head(entry);
    }
    std::pmr::vector<Node> allocated(&memory);
    std::size_t initialized = 0;
    try {
      output.reserve(changes.size()); allocated = nodes.batch(count, 0);
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
  bool install(Mutation& mutation, std::uint64_t cut) noexcept {
    auto& entry = entries.get(mutation.entry);
    if (mutation.old) nodes.get(mutation.old).older = entry.older;
    entry.older = mutation.old; entry.cut = cut;
    entry.primary = std::exchange(mutation.replacement, nullptr);
    entries.unpin(mutation.entry); ++versions;
    return entry.older || !entry.primary;
  }
  void abort(Mutation& mutation) noexcept {
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
  void drop(Ref version) noexcept {
    const auto node = node_of(version); assert(node);
    auto* value = primary(version);
    auto* alternative = alternate(version);
    alternatives.erase(key(version)); erase_borrowed(node);
    if (alternative) Physical::release(alternative);
    if (value) Physical::release(value);
  }
  void erase_head(ObjectHandle handle) noexcept {
    const auto current = head(handle);
    auto* value = current ? primary(current) : nullptr;
    auto* alternative = current ? alternate(current) : nullptr;
    if (current) alternatives.erase(key(current));
    const bool erased = entries.erase(handle); assert(erased); (void)erased;
    if (current) --versions;
    if (alternative) Physical::release(alternative);
    if (value) Physical::release(value);
  }
  orbital::Result<void> adopt(Ref version, Value* next) {
    try {
      auto [where, inserted] = alternatives.try_emplace(key(version), next); Physical::retain(next);
      if (!inserted) { auto* old = where->second; where->second = next; Physical::release(old); }
    } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
    return {};
  }
  void dispose() noexcept {
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
  std::size_t alternative_count() const noexcept { return alternatives.size(); }
  std::size_t history_capacity_bytes() const noexcept { return nodes.capacity_bytes(); }
  std::size_t descriptor_capacity_bytes() const noexcept { return directory.capacity_bytes(); }
  static constexpr std::size_t node_bytes = sizeof(typename Nodes::Node);
};

} // namespace compact_history
