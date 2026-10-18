#pragma once
#include <orbital/allocation.h>
#include <orbital/detail/object/binding.h>
#include <memory>
#include <memory_resource>

namespace orbital {
/// Embedded single-owner physical storage. The caller's static Control owns
/// interpretation, geometry and its dependencies; Orbital owns body lifetime.
/// Control operations are nonthrowing. They retain immutable admitted geometry.
/// Geometry queries read that proof without mutating capture sources or reentering
/// capture. Selected-copy storage requirements are part of the static seam.
/// The memory resource outlives every Representation and escaping Part; its
/// allocation/deallocation callbacks must not reenter the owning ObjectStore.
/// Physical-owner disposal callbacks may reenter the catalog during cleanup.
template<class Control> struct Representation;
template<class Control> struct Part;
template<class Control> struct RepresentationSelection;
struct PartSource;
namespace object_detail {
struct SourceAccess;
class RetainedSource {
  enum class Kind { none, shared, allocation };
  Bytes footprint_;
  Kind kind_{Kind::none};
  union {
    const std::shared_ptr<const void>* shared_{};
    allocation::Storage* allocation_;
  };
  friend struct SourceAccess;
};
}
enum class PartOwners : std::uint8_t { independent_shared, grouped_shared, independent_allocation, grouped_allocation, independent_mixed };
enum class PartRetention { shared_control, selected_copy };
struct PartSource {
  PartId id{};
  Bytes body;
  std::shared_ptr<const void> backing{};
  allocation::Allocation* allocation{};
  object_detail::RetainedSource retained{};
};

/// Pointer-only native Part borrow. Its live Selection/Control supplies extent
/// and interpretation. An empty Part still has a valid address slot.
struct View {
  const std::byte* const* slot{};
  explicit operator bool() const noexcept { return slot; }
  const std::byte* address() const noexcept { return *slot; }
};
template<class Control> struct RepresentationSelection {
  Representation<Control>* realization{};
  explicit operator bool() const noexcept { return realization; }
  Control control() const noexcept;
  std::size_t part_count() const noexcept;
  [[nodiscard]] Result<View> part(std::size_t) const noexcept;
  [[nodiscard]] Result<View> part(PartId) const noexcept;
};

/// Borrow a certified physical source through capture, without allocating or
/// retaining. Keep its Part or selected Representation and proof alive and fixed
/// through capture, including callbacks. The returned body/id may be changed;
/// new body+padding must fit the old footprint. New Control owns interpretation.
template<class Control> [[nodiscard]] Result<PartSource> source(const Part<Control>*) noexcept;
template<class Control> [[nodiscard]] Result<PartSource> source(RepresentationSelection<Control>, std::size_t) noexcept;

/// Reserve metadata and validate sources, then pin retained backing before
/// transferring fresh Allocation uses. A refusal leaves Allocations intact.
/// New Control independently preserves interpretation/disposal dependencies.
/// Metadata reservation can fail
/// before sources are checked. Capture seals the initialized body and readable
/// padding for its owners and derived access. The caller controls write authority
/// over physically disjoint storage. A shared backing owner must cover the full
/// readable footprint and keep its address stable; lifetime alone proves neither.
/// An Object/version label is never physical ownership authority.
template<class Control> [[nodiscard]] Result<Representation<Control>*> capture_representation(
    std::pmr::memory_resource&, Control, PartOwners, std::span<const PartSource>) noexcept;
template<class Control> void retain(Representation<Control>*) noexcept;
template<class Control> void release(Representation<Control>*) noexcept;
template<class Control> void retain(Part<Control>*) noexcept;
template<class Control> void release(Part<Control>*) noexcept;
template<class Control> View view(const Part<Control>*) noexcept;
template<class Control> ObjectBinding binding(const Part<Control>*) noexcept;
static_assert(sizeof(View) == 8 && sizeof(Result<View>) == 16);
} // namespace orbital
