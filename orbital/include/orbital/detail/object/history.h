#pragma once

#include "physical.h"
#include "versions.h"
#include "directory.h"
#include <unordered_map>

namespace orbital::object_detail {

// Current tips live in directory entries; older versions use 16-byte nodes.
// Ref is a private
// {entry handle, older-node handle}; node zero denotes that entry's current tip.
// It never escapes a protected Core operation. Selection belongs to the material.
template<class Physical> struct History {
  using Nodes = HistoryNodes;
  using Objects = TipObjects;
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

  std::pmr::memory_resource& memory;
  Nodes nodes;
  Registry directory;
  Objects entries;
  std::pmr::unordered_map<AlternativeKey, Value*, AlternativeHash> alternatives;
  std::size_t versions{};

  explicit History(std::pmr::memory_resource& resource)
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
  void reserve(std::span<const Change> changes);
  void erase_borrowed(Node node) noexcept {
    if (nodes.get(node).primary) directory.erase(nodes.get(node).primary);
    nodes.erase(node);
  }
  void prepare(std::span<const Change> changes, std::pmr::vector<Mutation>& output);
  bool install(Mutation& mutation, std::uint64_t cut) noexcept;
  void abort(Mutation& mutation) noexcept;
  void drop(Ref version) noexcept;
  void erase_head(ObjectHandle handle) noexcept;
  orbital::Result<void> adopt(Ref version, Value* next);
  void dispose() noexcept;
  std::size_t alternative_count() const noexcept { return alternatives.size(); }
  std::size_t history_capacity_bytes() const noexcept { return nodes.capacity_bytes(); }
  std::size_t descriptor_capacity_bytes() const noexcept { return directory.capacity_bytes(); }
  static constexpr std::size_t node_bytes = sizeof(typename Nodes::Node);
};

} // namespace orbital::object_detail
