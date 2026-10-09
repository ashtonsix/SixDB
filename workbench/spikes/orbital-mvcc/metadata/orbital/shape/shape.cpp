#include "shape.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

namespace compact_shape {
namespace {
enum class Kind : std::uint32_t { tuple, series };
bool shared(Owners owners) { return owners == Owners::independent_shared || owners == Owners::grouped_shared; }
bool grouped(Owners owners) { return owners == Owners::grouped_shared || owners == Owners::grouped_allocation; }
std::byte* bytes(void* p) { return static_cast<std::byte*>(p); }
const std::byte* bytes(const void* p) { return static_cast<const std::byte*>(p); }

bool valid(Bytes input, std::size_t padding = 0) {
  if (padding > SIZE_MAX - input.size()) return false;
  const auto extent = input.size() + padding;
  return !extent || (input.data() && extent <= UINTPTR_MAX - std::uintptr_t(input.data()));
}

bool placement(Bytes body, const Geometry& geometry, std::uint64_t count) {
  const auto extent = geometry.extent(count);
  return extent && *extent == body.size() && valid(body, geometry.readable_padding) &&
      ((!body.size() && !geometry.readable_padding) ||
       std::uintptr_t(body.data()) % (std::uint64_t{1} << geometry.alignment_log2) == 0);
}

bool unique(std::pmr::memory_resource& memory, std::span<const compact_descriptor::PartSource> parts) {
  if (parts.size() <= 8) {
    for (std::size_t i = 0; i < parts.size(); ++i)
      for (std::size_t j = 0; j < i; ++j) if (parts[i].id == parts[j].id) return false;
    return true;
  }
  std::pmr::vector<PartId> ids(&memory);
  ids.reserve(parts.size());
  for (const auto& part : parts) ids.push_back(part.id);
  std::sort(ids.begin(), ids.end());
  return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}
} // namespace

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

namespace {
struct SharedBody { const std::byte* address; std::shared_ptr<const void> owner; };
struct AllocatedBody { const std::byte* address; orbital::allocation::Storage* owner; };

Geometry* geometries(Shape* shape) {
  return static_cast<Geometry*>(static_cast<void*>(bytes(shape) + sizeof(Shape)));
}
const Geometry* geometries(const Shape* shape) { return geometries(const_cast<Shape*>(shape)); }
ReadLaw* law(Shape* shape) {
  return static_cast<ReadLaw*>(static_cast<void*>(bytes(shape) + sizeof(Shape) + shape->parts * sizeof(Geometry)));
}
Bytes wire(const Shape* shape) {
  if (shape->kind != Kind::tuple) return {};
  return {bytes(shape) + sizeof(Shape) + shape->parts * sizeof(Geometry) + sizeof(ReadLaw), 75};
}

Result<Shape*> make(std::pmr::memory_resource& memory, ProgramId program, Kind kind,
    std::span<const Geometry> geometry, const ReadLaw* read_law, Bytes interpretation,
    std::shared_ptr<const void> dependencies) {
  const auto extra = read_law ? sizeof(ReadLaw) + interpretation.size() : 0;
  if (geometry.size() > (UINT32_MAX - sizeof(Shape) - extra) / sizeof(Geometry))
    return std::unexpected(Error::capacity);
  const auto allocation = sizeof(Shape) + geometry.size() * sizeof(Geometry) + extra;
  auto* shape = std::construct_at(static_cast<Shape*>(memory.allocate(allocation, alignof(Shape))),
      Shape{&memory, 1, static_cast<std::uint32_t>(allocation), program,
            std::move(dependencies), static_cast<std::uint32_t>(geometry.size()), kind});
  for (std::size_t i = 0; i < geometry.size(); ++i) std::construct_at(geometries(shape) + i, geometry[i]);
  if (read_law) {
    std::construct_at(law(shape), *read_law);
    std::memcpy(bytes(shape) + sizeof(Shape) + geometry.size() * sizeof(Geometry) + sizeof(ReadLaw),
        interpretation.data(), interpretation.size());
  }
  return shape;
}

bool source_geometry(const compact_descriptor::PartSource& source) {
  return std::has_single_bit(source.alignment) && source.readable_padding <= UINT32_MAX &&
      valid(source.body, source.readable_padding) && valid(source.interpretation);
}

Result<Geometry> series_geometry(const compact_descriptor::PartSource& source, std::uint64_t& count) {
  if (!source_geometry(source)) return std::unexpected(Error::invalid_argument);
  const auto decoded = fixture::sp::decode_descriptor(
      {static_cast<const std::uint8_t*>(static_cast<const void*>(source.interpretation.data())), source.interpretation.size()});
  if (!decoded || decoded->heads || decoded->storage != fixture::sp::geometry::local ||
      decoded->strides != std::array<std::uint64_t, 3>{decoded->width, 0, 0})
    return std::unexpected(Error::invalid_argument);
  Geometry geometry{source.id, decoded->strides[0], static_cast<std::uint32_t>(source.readable_padding),
      8, static_cast<std::uint8_t>(std::countr_zero(source.alignment)), 1};
  count = decoded->count;
  if (!placement(source.body, geometry, count)) return std::unexpected(Error::invalid_argument);
  return geometry;
}

std::size_t realization_bytes(const Shape& shape, Owners owners) {
  if (grouped(owners)) return sizeof(Realization) + shape.parts * sizeof(const std::byte*) +
      (shape.parts ? (shared(owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)) : 0);
  return sizeof(Realization) + shape.parts * (shared(owners) ? sizeof(SharedBody) : sizeof(AllocatedBody));
}
const std::byte** addresses(Realization* value) {
  return static_cast<const std::byte**>(static_cast<void*>(bytes(value) + sizeof(Realization)));
}
SharedBody* shared_bodies(Realization* value) {
  return static_cast<SharedBody*>(static_cast<void*>(bytes(value) + sizeof(Realization)));
}
AllocatedBody* allocated_bodies(Realization* value) {
  return static_cast<AllocatedBody*>(static_cast<void*>(bytes(value) + sizeof(Realization)));
}
void* group_owner(Realization* value) { return bytes(value) + sizeof(Realization) + value->shape->parts * sizeof(const std::byte*); }

Result<orbital::allocation::Storage*> hold_source(orbital::allocation::Allocation* allocation) {
  if (!allocation || !*allocation) return static_cast<orbital::allocation::Storage*>(nullptr);
  auto retained = orbital::allocation::hold(*allocation, {0, allocation->size + allocation->readable_padding});
  if (!retained) return std::unexpected(retained.error());
  return std::exchange(retained->storage, nullptr);
}
void drop(orbital::allocation::Storage* owner) {
  orbital::allocation::Hold use;
  use.storage = owner;
  orbital::allocation::release(use);
}
orbital::allocation::Storage* pin(orbital::allocation::Storage* owner) {
  if (!owner) return nullptr;
  // Capture registered the actual full Allocation footprint. This local only
  // borrows its live physical token; retain never narrows Storage::held_extent.
  orbital::allocation::Hold borrowed;
  borrowed.storage = owner;
  auto retained = orbital::allocation::retain(borrowed);
  borrowed.storage = nullptr;
  return std::exchange(retained.storage, nullptr);
}

bool within(const orbital::allocation::Allocation& allocation, Bytes body, std::uint32_t padding) {
  if (!allocation || !valid(body, padding) || !valid(orbital::allocation::bytes(allocation), allocation.readable_padding)) return false;
  const auto begin = std::uintptr_t(allocation.address);
  const auto part = std::uintptr_t(body.data());
  const auto extent = allocation.size + allocation.readable_padding;
  return part >= begin && part - begin <= extent && body.size() + padding <= extent - (part - begin);
}

bool unique_allocations(std::pmr::memory_resource& memory, std::span<const BodySource> bodies) {
  const auto storage = [](const BodySource& body) { return body.allocation ? body.allocation->storage : nullptr; };
  if (bodies.size() <= 8) {
    for (std::size_t i = 0; i < bodies.size(); ++i)
      for (std::size_t j = 0; storage(bodies[i]) && j < i; ++j)
        if (storage(bodies[i]) == storage(bodies[j])) return false;
    return true;
  }
  std::pmr::vector<orbital::allocation::Storage*> owners(&memory);
  owners.reserve(bodies.size());
  for (const auto& body : bodies) if (auto* owner = storage(body)) owners.push_back(owner);
  std::sort(owners.begin(), owners.end(), std::less<orbital::allocation::Storage*>{});
  return std::adjacent_find(owners.begin(), owners.end()) == owners.end();
}

const std::byte* const* address_slot(Realization* realization, std::size_t index) {
  if (grouped(realization->owners)) return addresses(realization) + index;
  if (shared(realization->owners)) return &shared_bodies(realization)[index].address;
  return &allocated_bodies(realization)[index].address;
}
std::shared_ptr<const void>& shared_owner(Realization* value, std::size_t index) {
  if (grouped(value->owners)) return *static_cast<std::shared_ptr<const void>*>(group_owner(value));
  return shared_bodies(value)[index].owner;
}
orbital::allocation::Storage* allocation_owner(Realization* value, std::size_t index) {
  if (grouped(value->owners)) return *static_cast<orbital::allocation::Storage**>(group_owner(value));
  return allocated_bodies(value)[index].owner;
}
} // namespace

static_assert(sizeof(Shape) == 56 && sizeof(Realization) == 24);
static_assert(sizeof(SharedBody) == 24 && sizeof(AllocatedBody) == 16);

Result<std::uint64_t> Geometry::extent(std::uint64_t count) const noexcept {
  if (!tile_rows || !tile_bytes) return std::unexpected(Error::invalid_argument);
  const auto tiles = count / tile_rows + (count % tile_rows != 0);
  if (tiles > UINT64_MAX / tile_bytes) return std::unexpected(Error::capacity);
  return tiles * tile_bytes;
}

Result<Shape*> prove_fixture(std::pmr::memory_resource& memory, const fixture::Schema& schema,
    ProgramId program, Bytes parent, std::span<const compact_descriptor::PartSource> parts,
    std::shared_ptr<const void> dependencies) noexcept {
  if (!valid(parent) || parts.size() > fixture::field_count) return std::unexpected(Error::invalid_argument);
  std::array<fixture::ReadPart, fixture::field_count> placements;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (!source_geometry(parts[i])) return std::unexpected(Error::invalid_argument);
    placements[i] = {parts[i].id, parts[i].body, parts[i].interpretation};
  }
  auto checked = fixture::ReadData::bind(schema, parent, std::span(placements).first(parts.size()));
  if (!checked) return std::unexpected(checked.error());
  try {
    if (checked->format() == fixture::Format::columns)
      return prove_series(memory, program, parts, std::move(dependencies));
    const auto& part = parts.front();
    Geometry geometry{part.id, schema.tuple.bytes(), static_cast<std::uint32_t>(part.readable_padding),
        1, static_cast<std::uint8_t>(std::countr_zero(part.alignment)), 0};
    if (!placement(part.body, geometry, checked->size())) return std::unexpected(Error::invalid_argument);
    auto read_law = ReadLaw::from_schema(schema);
    if (!read_law) return std::unexpected(read_law.error());
    return make(memory, program, Kind::tuple, {&geometry, 1}, &*read_law, part.interpretation, std::move(dependencies));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

Result<Shape*> prove_series(std::pmr::memory_resource& memory, ProgramId program,
    std::span<const compact_descriptor::PartSource> parts, std::shared_ptr<const void> dependencies) noexcept {
  try {
    if (!unique(memory, parts)) return std::unexpected(Error::invalid_argument);
    std::pmr::vector<Geometry> geometry(&memory);
    geometry.reserve(parts.size());
    std::uint64_t records{};
    for (const auto& part : parts) {
      std::uint64_t count;
      auto decoded = series_geometry(part, count);
      if (!decoded || (!geometry.empty() && count != records)) return std::unexpected(Error::invalid_argument);
      records = count;
      geometry.push_back(*decoded);
    }
    return make(memory, program, Kind::series, geometry, nullptr, {}, std::move(dependencies));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

void retain(Shape* shape) noexcept {
  if (!shape) return;
  assert(shape->references != UINT32_MAX);
  ++shape->references;
}
void release(Shape* shape) noexcept {
  if (!shape || --shape->references) return;
  auto* resource = shape->resource;
  const auto size = shape->allocated_bytes;
  if (shape->kind == Kind::tuple) std::destroy_at(law(shape));
  std::destroy_at(shape);
  resource->deallocate(shape, size, alignof(Shape));
}
std::size_t part_count(const Shape* shape) noexcept { return shape->parts; }
const Geometry& geometry(const Shape* shape, std::size_t part) noexcept { return geometries(shape)[part]; }
ProgramId program(const Shape* shape) noexcept { return shape->program; }
const ReadLaw& tuple_law(const Shape* shape) noexcept { assert(shape->kind == Kind::tuple); return *law(const_cast<Shape*>(shape)); }
Bytes tuple_interpretation(const Shape* shape) noexcept { return wire(shape); }

Result<Realization*> capture(std::pmr::memory_resource& memory, const Specification& spec,
    std::span<const BodySource> bodies) noexcept {
  auto* shape = spec.shape;
  if (!shape || shape->resource != &memory || bodies.size() != shape->parts ||
      (!shape->parts && spec.records) || static_cast<unsigned>(spec.owners) > 3)
    return std::unexpected(Error::invalid_argument);
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const auto& body = bodies[i];
    const auto& geometry = geometries(shape)[i];
    if (body.id != geometry.id || !placement(body.body, geometry, spec.records)) return std::unexpected(Error::invalid_argument);
    if (shared(spec.owners)) {
      if ((body.body.size() || geometry.readable_padding) && !body.backing.use_count()) return std::unexpected(Error::invalid_argument);
      if (grouped(spec.owners) && i &&
          (body.backing.owner_before(bodies[0].backing) || bodies[0].backing.owner_before(body.backing)))
        return std::unexpected(Error::invalid_argument);
    } else {
      if ((body.body.size() || geometry.readable_padding) &&
          (!body.allocation || !within(*body.allocation, body.body, geometry.readable_padding)))
        return std::unexpected(Error::invalid_argument);
      if (grouped(spec.owners) && i && body.allocation != bodies[0].allocation) return std::unexpected(Error::invalid_argument);
    }
  }
  const auto size = realization_bytes(*shape, spec.owners);
  try {
    if (!shared(spec.owners) && !grouped(spec.owners) && !unique_allocations(memory, bodies))
      return std::unexpected(Error::invalid_argument);
    auto* value = std::construct_at(static_cast<Realization*>(memory.allocate(size, alignof(Realization))),
        Realization{shape, spec.records, 1, spec.owners});
    retain(shape);
    if (grouped(spec.owners)) {
      for (std::size_t i = 0; i < bodies.size(); ++i) std::construct_at(addresses(value) + i, bodies[i].body.data());
      if (!bodies.empty()) {
        if (shared(spec.owners)) std::construct_at(static_cast<std::shared_ptr<const void>*>(group_owner(value)), bodies[0].backing);
        else {
          auto* owner = static_cast<orbital::allocation::Storage**>(group_owner(value));
          *owner = nullptr;
          auto held = hold_source(bodies[0].allocation);
          if (!held) { release(value); return std::unexpected(held.error()); }
          *owner = *held;
        }
      }
    } else {
      for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (shared(spec.owners)) std::construct_at(shared_bodies(value) + i, SharedBody{bodies[i].body.data(), bodies[i].backing});
        else std::construct_at(allocated_bodies(value) + i, AllocatedBody{bodies[i].body.data(), nullptr});
      }
      if (!shared(spec.owners)) {
        for (std::size_t i = 0; i < bodies.size(); ++i) {
          auto held = hold_source(bodies[i].allocation);
          if (!held) { release(value); return std::unexpected(held.error()); }
          allocated_bodies(value)[i].owner = *held;
        }
      }
    }
    if (!shared(spec.owners)) {
      const auto originals = grouped(spec.owners) ? std::min<std::size_t>(1, bodies.size()) : bodies.size();
      for (std::size_t i = 0; i < originals; ++i)
        if (bodies[i].allocation) orbital::allocation::release(*bodies[i].allocation);
    }
    return value;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

void retain(Realization* value) noexcept { if (value) { assert(value->references != UINT32_MAX); ++value->references; } }
void release(Realization* value) noexcept {
  if (!value || --value->references) return;
  auto* shape = value->shape;
  const auto size = realization_bytes(*shape, value->owners);
  if (grouped(value->owners)) {
    if (shape->parts) {
      if (shared(value->owners)) std::destroy_at(static_cast<std::shared_ptr<const void>*>(group_owner(value)));
      else drop(*static_cast<orbital::allocation::Storage**>(group_owner(value)));
    }
  } else {
    for (std::size_t i = 0; i < shape->parts; ++i) {
      if (shared(value->owners)) std::destroy_at(shared_bodies(value) + i);
      else drop(allocated_bodies(value)[i].owner);
    }
  }
  std::destroy_at(value);
  shape->resource->deallocate(value, size, alignof(Realization));
  release(shape);
}
std::uint64_t records(const Realization* value) noexcept { return value->count; }
std::pmr::memory_resource* resource(const Realization* value) noexcept { return value->shape->resource; }
bool equivalent(const Realization* a, const Realization* b) noexcept {
  return a->count == b->count && a->shape->program == b->shape->program;
}
std::uint64_t Selection::records() const noexcept { return realization->count; }
const Shape* Selection::shape() const noexcept { return realization->shape; }
Control Selection::control() const noexcept { return {realization->shape, realization->count}; }
std::size_t Selection::part_count() const noexcept { return realization ? realization->shape->parts : 0; }
Result<View> Selection::part(std::size_t index) const noexcept {
  if (!realization || index >= realization->shape->parts) return std::unexpected(Error::absent);
  return View{address_slot(realization, index)};
}
Result<View> Selection::part(PartId id) const noexcept {
  if (!realization) return std::unexpected(Error::absent);
  for (std::size_t i = 0; i < realization->shape->parts; ++i)
    if (geometries(realization->shape)[i].id == id) return View{address_slot(realization, i)};
  return std::unexpected(Error::absent);
}

namespace {
union ExportOwner {
  std::shared_ptr<const void> shared;
  orbital::allocation::Storage* allocation;
  ExportOwner() {}
  ~ExportOwner() {}
};
} // namespace
struct Export {
  Shape* shape;
  std::uint64_t count;
  const std::byte* address;
  Binding binding;
  std::uint32_t references{1}, index;
  Owners owners;
  Interpretation interpretation;
  ExportOwner owner;
};
static_assert(sizeof(Export) == 80);

Result<Export*> detail::retain_part(Selection selection, std::size_t index, Binding binding, Interpretation interpretation) noexcept {
  if (interpretation != Interpretation::shared_shape && interpretation != Interpretation::selected_copy)
    return std::unexpected(Error::invalid_argument);
  if (!selection || index >= selection.part_count()) return std::unexpected(Error::absent);
  auto* realization = selection.realization;
  auto* source = realization->shape;
  const auto& part = geometries(source)[index];
  auto* memory = source->resource;
  const auto copy_bytes = sizeof(Shape) + sizeof(Geometry) + (source->kind == Kind::tuple ? sizeof(ReadLaw) + wire(source).size() : 0);
  const auto size = sizeof(Export) + (interpretation == Interpretation::selected_copy ? copy_bytes : 0);
  Export* value = nullptr;
  try {
    value = static_cast<Export*>(memory->allocate(size, alignof(Export)));
    std::construct_at(value);
    value->shape = source; value->count = realization->count;
    value->address = *address_slot(realization, index); value->binding = binding;
    value->index = index; value->owners = realization->owners; value->interpretation = interpretation;
    if (shared(value->owners)) std::construct_at(&value->owner.shared, shared_owner(realization, index));
    else value->owner.allocation = pin(allocation_owner(realization, index));
    if (interpretation == Interpretation::shared_shape) retain(source);
    else {
      auto* copy = std::construct_at(static_cast<Shape*>(static_cast<void*>(bytes(value) + sizeof(Export))),
          Shape{memory, 1, static_cast<std::uint32_t>(copy_bytes), source->program, source->dependencies, 1, source->kind});
      std::construct_at(geometries(copy), part);
      if (source->kind == Kind::tuple) {
        std::construct_at(law(copy), *law(source));
        std::memcpy(bytes(copy) + sizeof(Shape) + sizeof(Geometry) + sizeof(ReadLaw), wire(source).data(), wire(source).size());
      }
      value->shape = copy; value->index = 0;
    }
    return value;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}
void retain(Export* value) noexcept { if (value) { assert(value->references != UINT32_MAX); ++value->references; } }
void release(Export* value) noexcept {
  if (!value || --value->references) return;
  auto* shape = value->shape;
  auto* memory = shape->resource;
  const auto size = sizeof(Export) + (value->interpretation == Interpretation::selected_copy ? shape->allocated_bytes : 0);
  const auto copied = value->interpretation == Interpretation::selected_copy;
  if (shared(value->owners)) std::destroy_at(&value->owner.shared);
  else drop(value->owner.allocation);
  std::destroy_at(value);
  if (copied) {
    if (shape->kind == Kind::tuple) std::destroy_at(law(shape));
    std::destroy_at(shape);
  }
  memory->deallocate(value, size, alignof(Export));
  if (!copied) release(shape);
}
View view(const Export* value) noexcept { return {&value->address}; }
Binding binding(const Export* value) noexcept { return value->binding; }
std::uint64_t records(const Export* value) noexcept { return value->count; }
const Geometry& geometry(const Export* value) noexcept { return geometries(value->shape)[value->index]; }
const ReadLaw& tuple_law(const Export* value) noexcept { return tuple_law(value->shape); }
Bytes tuple_interpretation(const Export* value) noexcept { return wire(value->shape); }
Result<fixture::sp::descriptor_bytes> series_interpretation(const Export* value) noexcept {
  if (value->shape->kind != Kind::series) return std::unexpected(Error::invalid_argument);
  const auto& part = geometry(value);
  auto encoded = fixture::sp::encode_descriptor({static_cast<unsigned>(part.tile_bytes), 0,
      fixture::sp::geometry::local, value->count, {part.tile_bytes, 0, 0}});
  if (!encoded) return std::unexpected(Error::invalid_argument);
  return *encoded;
}

Footprint footprint(const Realization* value) noexcept {
  const auto owners = (grouped(value->owners) ? (value->shape->parts ? 1 : 0) : value->shape->parts) *
      (shared(value->owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*));
  return {realization_bytes(*value->shape, value->owners), 0, value->shape->allocated_bytes, owners};
}
Footprint footprint(const Export* value) noexcept {
  const auto copied = value->interpretation == Interpretation::selected_copy;
  return {sizeof(Export) + (copied ? value->shape->allocated_bytes : 0),
      copied ? wire(value->shape).size() : 0, copied ? 0 : value->shape->allocated_bytes,
      shared(value->owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)};
}
std::size_t shape_bytes(const Shape* shape) noexcept { return shape->allocated_bytes; }

} // namespace compact_shape
