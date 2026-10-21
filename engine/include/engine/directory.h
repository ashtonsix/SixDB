#pragma once
#include <engine/material.h>
#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace loom { struct Worker; }
namespace engine::directory {
/// Native process-local locator recipe for this increment. Other locator recipes
/// require their own interpretation; these fields are not a persistent format.
struct Dirent {
  std::array<std::uint64_t,6> parts{};
  std::uint32_t representation{};
  std::uint16_t mapped{},extra_rank{};
  std::uint32_t extra_block{},reserved{};
  friend bool operator==(const Dirent&,const Dirent&)=default;
};
static_assert(sizeof(Dirent)==64);
struct Entry { std::uint32_t prefix{}; Dirent value; };
/// Logical work counters, including work performed before refusal. allocations
/// counts native body requests; proof_bytes excludes Bundle/shared-control-block,
/// capture/catalog metadata, allocator capacity, padding and retained backing.
struct Work {
  std::uint64_t allocations{},body_bytes{},copied_bytes{},captures{},proof_bytes{},child_retains{};
};
namespace detail { struct Node; struct State; }
/// Borrowed for the issuing Scope's lifetime. The same Scope protects separately
/// versioned Segment material reached by the locators, including late discovery.
class Read {
  friend class Directory;
  const detail::Node* root_{};
  explicit Read(const detail::Node* p):root_(p) {}
public:
  const Dirent* find(std::uint32_t prefix) const noexcept;
  /// Independent physical probes in input order, null for misses. The output
  /// holds Scope-borrowed pointers; input/output storage must not overlap.
  /// Capacity refusal occurs before writing output. No per-probe enrollment.
  orbital::Result<void> find_many(std::span<const std::uint32_t>,
      std::span<const Dirent*>) const noexcept;
};
/// First native physical-prefix path. Ordered logical-key routing, membership
/// edits, ordinary/hash recipes, extras and schema are not implemented here yet.
/// The caller owns Store/Core, Scope and the ONE joined publication packet.
class Directory {
  std::unique_ptr<detail::State> state_;
  explicit Directory(std::unique_ptr<detail::State>);
public:
  ~Directory();
  Directory(const Directory&)=delete;
  Directory& operator=(const Directory&)=delete;
  static orbital::Result<std::unique_ptr<Directory>> open(material::Store&,
      loom::Worker&,std::pmr::memory_resource&,std::uint64_t object_namespace);
  orbital::ObjectId root_id(unsigned layer) const noexcept;
  /// Sorted unique physical prefixes in one layer. Returns a private finished
  /// root; no intermediate setup or publication occurs inside the Directory.
  orbital::Result<material::Store::Owner> build(unsigned layer,std::span<const Entry>,Work* = nullptr);
  orbital::Result<Read> read(const material::Store::Scope&,unsigned layer) const noexcept;
  /// Existing membership only. Returns one finished replacement root. A refused
  /// or discarded candidate publishes nothing. Does not capture Segment material.
  orbital::Result<material::Store::Owner> replace(const material::Store::Scope&,
      unsigned layer,std::uint32_t prefix,const Dirent&,Work* = nullptr);
  /// Final intents sorted by unique prefix. Copies each affected directory
  /// group/routing node once. Finished Segment changes join this root externally.
  orbital::Result<material::Store::Owner> replace_many(const material::Store::Scope&,
      unsigned layer,std::span<const Entry>,Work* = nullptr);
};
}
