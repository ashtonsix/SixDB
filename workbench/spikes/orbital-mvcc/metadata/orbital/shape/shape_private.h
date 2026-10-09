#pragma once

#include "shape.h"

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

struct Realization {
  Shape* shape;
  std::uint64_t count;
  std::uint32_t references{1};
  Owners owners;
};

struct SharedBody { const std::byte* address; std::shared_ptr<const void> owner; };
struct AllocatedBody { const std::byte* address; orbital::allocation::Storage* owner; };

inline bool shared(Owners owners) {
  return owners == Owners::independent_shared || owners == Owners::grouped_shared;
}
inline bool grouped(Owners owners) {
  return owners == Owners::grouped_shared || owners == Owners::grouped_allocation;
}
inline std::byte* bytes(void* p) { return static_cast<std::byte*>(p); }
inline const std::byte* bytes(const void* p) { return static_cast<const std::byte*>(p); }

inline Geometry* geometries(Shape* shape) {
  return static_cast<Geometry*>(static_cast<void*>(bytes(shape) + sizeof(Shape)));
}
inline const Geometry* geometries(const Shape* shape) { return geometries(const_cast<Shape*>(shape)); }
inline ReadLaw* law(Shape* shape) {
  return static_cast<ReadLaw*>(static_cast<void*>(bytes(shape) + sizeof(Shape) + shape->parts * sizeof(Geometry)));
}
inline const std::byte** addresses(Realization* value) {
  return static_cast<const std::byte**>(static_cast<void*>(bytes(value) + sizeof(Realization)));
}
inline SharedBody* shared_bodies(Realization* value) {
  return static_cast<SharedBody*>(static_cast<void*>(bytes(value) + sizeof(Realization)));
}
inline AllocatedBody* allocated_bodies(Realization* value) {
  return static_cast<AllocatedBody*>(static_cast<void*>(bytes(value) + sizeof(Realization)));
}
inline void* group_owner(Realization* value) {
  return bytes(value) + sizeof(Realization) + value->shape->parts * sizeof(const std::byte*);
}
inline const std::byte* const* address_slot(Realization* value, std::size_t index) {
  if (grouped(value->owners)) return addresses(value) + index;
  if (shared(value->owners)) return &shared_bodies(value)[index].address;
  return &allocated_bodies(value)[index].address;
}

static_assert(sizeof(Shape) == 56 && sizeof(Realization) == 24);
static_assert(sizeof(SharedBody) == 24 && sizeof(AllocatedBody) == 16);

} // namespace compact_shape
