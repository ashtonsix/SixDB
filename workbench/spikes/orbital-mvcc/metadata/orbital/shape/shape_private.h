#pragma once

#include "shape.h"
#include <orbital/detail/object/representation.h>

// The composed native consumer deliberately depends on this actual layout.
// Keep addressing here so independent TUs share one definition and inline the
// same helpers; public users continue to borrow the opaque Selection/View API.
namespace compact_shape {

enum class Kind : std::uint32_t { tuple, series };

struct Shape {
  std::pmr::memory_resource* resource;
  std::uint32_t references{1}, allocated_bytes;
  ProgramId program;
  std::shared_ptr<const void> dependencies;
  std::uint32_t parts;
  Kind kind;
};

using orbital::object_detail::bytes;
using orbital::object_detail::SharedBody;
using orbital::object_detail::AllocatedBody;
using orbital::object_detail::address_slot;
using orbital::object_detail::shared;
using orbital::object_detail::grouped;

inline Geometry* geometries(Shape* shape) {
  return static_cast<Geometry*>(static_cast<void*>(bytes(shape) + sizeof(Shape)));
}
inline const Geometry* geometries(const Shape* shape) { return geometries(const_cast<Shape*>(shape)); }
inline ReadLaw* law(Shape* shape) {
  return static_cast<ReadLaw*>(static_cast<void*>(bytes(shape) + sizeof(Shape) + shape->parts * sizeof(Geometry)));
}
inline std::size_t Control::part_count() const noexcept { return shape->parts; }
inline std::pmr::memory_resource* Control::resource() const noexcept { return shape->resource; }
inline PartId Control::id(std::size_t index) const noexcept { return geometries(shape)[index].id; }
inline Result<std::uint64_t> Control::extent(std::size_t index) const noexcept { return geometries(shape)[index].extent(records); }
inline std::uint32_t Control::padding(std::size_t index) const noexcept { return geometries(shape)[index].readable_padding; }
inline std::uint64_t Control::alignment(std::size_t index) const noexcept { return std::uint64_t{1} << geometries(shape)[index].alignment_log2; }

static_assert(offsetof(Realization, control) == 0 && offsetof(Realization, references) == 16 && offsetof(Realization, owners) == 20);
static_assert(sizeof(Export) == 80 && offsetof(Export, address) == 16 && offsetof(Export, binding) == 24 && offsetof(Export, owner) == 64);
static_assert(sizeof(Shape) == 56 && sizeof(Realization) == 24);
static_assert(sizeof(SharedBody) == 24 && sizeof(AllocatedBody) == 16);

} // namespace compact_shape
