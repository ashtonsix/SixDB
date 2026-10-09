#pragma once

#include "versions.h"
#include "physical.h"
#include "object_policy.h"

namespace compact_history {

struct EmptyHistoryField {};

// Existing control layout. The protocol owns reader cuts and accepted episodes;
// this layout owns stable entries, version slots and physical references.
template<class Nodes, class Physical, class Objects> struct NodeHistory {
  using Value = typename Physical::Value;
  using Selection = typename Physical::Selection;
  using Change = ChangeOf<PhysicalOwner<Physical>>;
  using Entry = typename Objects::Entry;
  using ObjectHandle = typename Objects::Handle;
  using Ref = typename Nodes::Handle;
  struct Mutation { ObjectHandle entry; Ref version; };
  using AlternativeMap = std::pmr::unordered_map<Ref, Value*>;

  compact_version_probe::Memory& memory;
  Nodes nodes;
  [[no_unique_address]] std::conditional_t<Nodes::indexed_rep, compact_version_probe::Registry,
      EmptyHistoryField> directory;
  Objects entries;
  [[no_unique_address]] std::conditional_t<Nodes::inline_alternative, EmptyHistoryField, AlternativeMap> alternatives;
  std::size_t versions{};

  explicit NodeHistory(compact_version_probe::Memory& resource) : memory(resource), nodes(resource),
      directory([&] { if constexpr (Nodes::indexed_rep) return compact_version_probe::Registry(resource);
        else return EmptyHistoryField{}; }()), entries(resource),
      alternatives([&] { if constexpr (Nodes::inline_alternative) return EmptyHistoryField{};
        else return AlternativeMap(&resource); }()) {}

  Ref head(ObjectHandle entry) const noexcept { return entries.get(entry).head; }
  Ref older(Ref version) const noexcept { return nodes.get(version).older; }
  void link_older(Ref version, Ref old) noexcept { nodes.get(version).older = old; }
  std::uint64_t version_cut(Ref version) const noexcept { return nodes.cut(version); }
  Value* primary(Ref version) const noexcept {
    if constexpr (Nodes::indexed_rep) return static_cast<Value*>(directory.get(nodes.get(version).primary));
    else return static_cast<Value*>(nodes.get(version).primary);
  }
  Value* alternate(Ref version) const noexcept {
    if constexpr (Nodes::inline_alternative) return static_cast<Value*>(nodes.get(version).alternative);
    else { auto at = alternatives.find(version); return at == alternatives.end() ? nullptr : at->second; }
  }
  void initialize(Ref version, Value* value) {
    if constexpr (Nodes::indexed_rep) nodes.get(version).primary = value ? directory.add(value) : 0;
    else nodes.get(version).primary = value;
    if (value) Physical::retain(value);
  }
  void drop(Ref version) noexcept {
    auto* value = primary(version);
    Value* alternative{};
    if constexpr (Nodes::indexed_rep) if (nodes.get(version).primary) directory.erase(nodes.get(version).primary);
    if constexpr (Nodes::inline_alternative) alternative = static_cast<Value*>(nodes.get(version).alternative);
    else {
      auto at = alternatives.find(version);
      if (at != alternatives.end()) { alternative = at->second; alternatives.erase(at); }
    }
    nodes.erase(version);
    if (alternative) Physical::release(alternative);
    if (value) Physical::release(value);
  }
  Ref locate(std::uint64_t cut, orbital::ObjectId id) noexcept {
    auto entry = entries.find(id);
    auto version = entry ? head(entry) : Ref{};
    while (version && version_cut(version) > cut) version = older(version);
    return version;
  }
  Selection resolve(std::uint64_t cut, orbital::ObjectId id, bool alternative) noexcept {
    const auto version = locate(cut, id);
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
  void prepare(std::span<const Change> changes, std::uint64_t cut, std::pmr::vector<Mutation>& output) {
    std::pmr::vector<Ref> allocated(&memory);
    std::size_t initialized = 0;
    try {
      output.reserve(changes.size());
      allocated = nodes.batch(changes.size(), cut);
      for (std::size_t i = 0; i < changes.size(); ++i) {
        initialize(allocated[i], changes[i].representation.get()); ++initialized;
        auto [entry, inserted] = entries.ensure(changes[i].object); (void)inserted;
        output.push_back({entry, allocated[i]});
      }
    } catch (...) {
      for (std::size_t i = 0; i < allocated.size(); ++i)
        if (i < initialized) drop(allocated[i]); else nodes.erase(allocated[i]);
      for (const auto& change : changes) {
        auto entry = entries.find(change.object);
        if (entry && !head(entry) && !entries.pinned(entry)) {
          const bool erased = entries.erase(entry); assert(erased); (void)erased;
        }
      }
      throw;
    }
    for (auto& change : output) { const bool pinned = entries.pin(change.entry); assert(pinned); (void)pinned; }
  }
  // Linking transfers already-owned slots; no allocation or owner destruction.
  bool install(Mutation& change, std::uint64_t) noexcept {
    auto& entry = entries.get(change.entry);
    link_older(change.version, entry.head); entry.head = change.version;
    entries.unpin(change.entry); ++versions;
    return older(change.version) || !primary(change.version);
  }
  void abort(Mutation& change) noexcept {
    const auto handle = std::exchange(change.entry, ObjectHandle{});
    auto& entry = entries.get(handle);
    const auto version = std::exchange(change.version, Ref{});
    assert(entries.pinned(handle)); entries.unpin(handle);
    if (!entry.head) {
      entries.remove_debt(handle); const bool erased = entries.erase(handle); assert(erased); (void)erased;
    } else if (!primary(entry.head)) entries.add_debt(handle);
    drop(version); // Entry uses end before any physical owner callback.
  }
  void erase_head(ObjectHandle entry) noexcept {
    const auto version = head(entry);
    const bool erased = entries.erase(entry); assert(erased); (void)erased;
    if (version) { --versions; drop(version); }
  }
  orbital::Result<void> adopt(Ref version, Value* next) {
    if constexpr (Nodes::inline_alternative) {
      Physical::retain(next);
      auto* old = static_cast<Value*>(nodes.get(version).alternative); nodes.get(version).alternative = next;
      if (old) Physical::release(old);
    } else {
      try {
        auto [where, inserted] = alternatives.try_emplace(version, next); Physical::retain(next);
        if (!inserted) { auto* old = where->second; where->second = next; Physical::release(old); }
      } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
    }
    return {};
  }
  void dispose() noexcept {
    entries.for_each_live([&](ObjectHandle entry, Entry&) {
      auto version = head(entry);
      while (version) { const auto old = older(version); drop(version); version = old; }
    });
  }
  std::size_t alternative_count() {
    if constexpr (!Nodes::inline_alternative) return alternatives.size();
    else {
      std::size_t result = 0;
      entries.for_each_live([&](ObjectHandle entry, Entry&) {
        for (auto version = head(entry); version; version = older(version)) result += alternate(version) != nullptr;
      });
      return result;
    }
  }
  std::size_t history_capacity_bytes() const noexcept { return nodes.capacity_bytes(); }
  std::size_t descriptor_capacity_bytes() const noexcept {
    if constexpr (Nodes::indexed_rep) return directory.capacity_bytes(); else return 0;
  }
  static constexpr std::size_t node_bytes = sizeof(typename Nodes::Node);
};

} // namespace compact_history
