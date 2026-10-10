#pragma once
#include <orbital/allocation.h>
#include <orbital/detail/object/binding.h>
#include <memory>
#include <memory_resource>

namespace orbital {
/// Embedded single-owner physical storage. The caller's static Control owns
/// interpretation, geometry and its dependencies; Orbital owns body lifetime.
/// Control operations are nonthrowing. They retain the exact admitted proof;
/// geometry and selected-copy storage requirements are part of that static seam.
/// The memory resource outlives every Representation and escaping Part; its
/// allocation/deallocation callbacks must not reenter the owning ObjectStore.
/// Physical-owner disposal callbacks may reenter the catalog during cleanup.
template<class Control> struct Representation;
template<class Control> struct Part;
enum class PartOwners { independent_shared, grouped_shared, independent_allocation, grouped_allocation };
enum class PartRetention { shared_control, selected_copy };
struct PartSource {
  PartId id{};
  Bytes body;
  std::shared_ptr<const void> backing;
  allocation::Allocation* allocation{};
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

/// Establish full-allocation Holds before relinquishing any supplied Allocation.
/// A refusal leaves original Allocations intact. Writable aliases end before
/// capture; an Object/version label is never physical ownership authority.
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
