#include "shape_private.h"
#include <orbital/instantiate/representation.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

namespace compact_shape {
namespace {
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

namespace {
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

} // namespace

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

bool Control::valid(std::pmr::memory_resource& memory) const noexcept {
  return shape && shape->resource == &memory && (shape->parts || !records);
}
std::size_t Control::part_count() const noexcept { return shape->parts; }
void Control::retain() const noexcept { compact_shape::retain(shape); }
void Control::release() const noexcept { compact_shape::release(shape); }
std::size_t Control::metadata_bytes() const noexcept { return shape->allocated_bytes; }
std::size_t Control::selected_bytes(std::size_t) const noexcept {
  return sizeof(Shape) + sizeof(Geometry) + (shape->kind == Kind::tuple ? sizeof(ReadLaw) + wire(shape).size() : 0);
}
Control Control::copy_selected(void* destination, std::size_t index) const noexcept {
  auto* copy = std::construct_at(static_cast<Shape*>(destination),
      Shape{shape->resource, 1, static_cast<std::uint32_t>(selected_bytes(index)), shape->program,
            shape->dependencies, 1, shape->kind});
  std::construct_at(geometries(copy), geometries(shape)[index]);
  if (shape->kind == Kind::tuple) {
    std::construct_at(law(copy), *law(shape));
    std::memcpy(bytes(copy) + sizeof(Shape) + sizeof(Geometry) + sizeof(ReadLaw), wire(shape).data(), wire(shape).size());
  }
  return {copy, records};
}
void Control::destroy_selected() const noexcept {
  if (shape->kind == Kind::tuple) std::destroy_at(law(shape));
  std::destroy_at(shape);
}
Result<Realization*> capture(std::pmr::memory_resource& memory, const Specification& spec,
    std::span<const BodySource> bodies) noexcept {
  return orbital::capture_representation(memory, Control{spec.shape, spec.records}, spec.owners, bodies);
}
std::uint64_t Selection::records() const noexcept { return realization->control.records; }
const Shape* Selection::shape() const noexcept { return realization->control.shape; }
std::uint64_t records(const Realization* value) noexcept { return value->control.records; }
std::pmr::memory_resource* resource(const Realization* value) noexcept { return value->control.resource(); }
bool equivalent(const Realization* a, const Realization* b) noexcept {
  return a->control.records == b->control.records && a->control.shape->program == b->control.shape->program;
}
Result<Export*> detail::retain_part(Selection selection, std::size_t index, Binding binding, Interpretation policy) noexcept {
  return orbital::object_detail::retain_part<Control>(selection, index, binding, policy);
}
std::uint64_t records(const Export* value) noexcept { return value->control.records; }
const Geometry& geometry(const Export* value) noexcept { return geometries(value->control.shape)[value->index]; }
const ReadLaw& tuple_law(const Export* value) noexcept { return tuple_law(value->control.shape); }
Bytes tuple_interpretation(const Export* value) noexcept { return wire(value->control.shape); }
Result<fixture::sp::descriptor_bytes> series_interpretation(const Export* value) noexcept {
  if (value->control.shape->kind != Kind::series) return std::unexpected(Error::invalid_argument);
  const auto& part = geometry(value);
  auto encoded = fixture::sp::encode_descriptor({static_cast<unsigned>(part.tile_bytes), 0,
      fixture::sp::geometry::local, value->control.records, {part.tile_bytes, 0, 0}});
  if (!encoded) return std::unexpected(Error::invalid_argument);
  return *encoded;
}

Footprint footprint(const Realization* value) noexcept {
  const auto owners = (grouped(value->owners) ? (value->control.shape->parts ? 1 : 0) : value->control.shape->parts) *
      (shared(value->owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*));
  return {orbital::object_detail::representation_bytes(value->control, value->owners), 0, value->control.shape->allocated_bytes, owners};
}
Footprint footprint(const Export* value) noexcept {
  const auto copied = value->retention == Interpretation::selected_copy;
  return {sizeof(Export) + (copied ? value->control.shape->allocated_bytes : 0),
      copied ? wire(value->control.shape).size() : 0, copied ? 0 : value->control.shape->allocated_bytes,
      shared(value->owners) ? sizeof(std::shared_ptr<const void>) : sizeof(void*)};
}
std::size_t shape_bytes(const Shape* shape) noexcept { return shape->allocated_bytes; }

} // namespace compact_shape

namespace orbital {
template struct RepresentationSelection<compact_shape::Control>;
template Result<Representation<compact_shape::Control>*> capture_representation(
    std::pmr::memory_resource&, compact_shape::Control, PartOwners, std::span<const PartSource>) noexcept;
template void retain(Representation<compact_shape::Control>*) noexcept;
template void release(Representation<compact_shape::Control>*) noexcept;
template void retain(Part<compact_shape::Control>*) noexcept;
template void release(Part<compact_shape::Control>*) noexcept;
template View view(const Part<compact_shape::Control>*) noexcept;
template ObjectBinding binding(const Part<compact_shape::Control>*) noexcept;
} // namespace orbital
