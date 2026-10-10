#pragma once

#include "../descriptor/descriptor.h"
#include "../../consumer/compact_law.h"
#include <orbital/representation.h>

namespace compact_shape {
using orbital::Bytes;
using orbital::Error;
using orbital::ObjectId;
using orbital::PartId;
using orbital::ProgramId;
using orbital::Result;
using orbital_mvcc::metadata_consumer::compact_law::ReadLaw;
namespace fixture = orbital_bench::versioned;

struct Shape;

struct Geometry {
  PartId id;
  std::uint64_t tile_bytes;
  std::uint32_t readable_padding;
  std::uint16_t tile_rows;
  std::uint8_t alignment_log2;
  std::uint8_t kind;
  [[nodiscard]] Result<std::uint64_t> extent(std::uint64_t records) const noexcept;
};

// Every supplied parent/Part descriptor and physical placement is admitted by
// the actual Ikea consumer before this shared proof is created. Schema is only
// a construction input; the Shape retains its extracted read controls.
[[nodiscard]] Result<Shape*> prove_fixture(std::pmr::memory_resource&,
    const fixture::Schema&, ProgramId, Bytes parent,
    std::span<const compact_descriptor::PartSource>,
    std::shared_ptr<const void> dependencies = {}) noexcept;

// A separate SeriesPack shape control supports arbitrary Part counts, including
// the 17-Part retention case. All Parts must be tight headless Local, have one
// consistent 64-bit count and unique IDs; every descriptor is really decoded.
[[nodiscard]] Result<Shape*> prove_series(std::pmr::memory_resource&, ProgramId,
    std::span<const compact_descriptor::PartSource>,
    std::shared_ptr<const void> dependencies = {}) noexcept;
void retain(Shape*) noexcept;
void release(Shape*) noexcept;
std::size_t part_count(const Shape*) noexcept;
const Geometry& geometry(const Shape*, std::size_t) noexcept;
ProgramId program(const Shape*) noexcept;
const ReadLaw& tuple_law(const Shape*) noexcept;
Bytes tuple_interpretation(const Shape*) noexcept;

using Owners = orbital::PartOwners;
using Interpretation = orbital::PartRetention;
using BodySource = orbital::PartSource;
using View = orbital::View;
using Binding = orbital::ObjectBinding;
struct Control {
  Shape* shape;
  std::uint64_t records;
  bool valid(std::pmr::memory_resource&) const noexcept;
  std::pmr::memory_resource* resource() const noexcept;
  std::size_t part_count() const noexcept;
  PartId id(std::size_t) const noexcept;
  Result<std::uint64_t> extent(std::size_t) const noexcept;
  std::uint32_t padding(std::size_t) const noexcept;
  std::uint64_t alignment(std::size_t) const noexcept;
  void retain() const noexcept;
  void release() const noexcept;
  std::size_t selected_bytes(std::size_t) const noexcept;
  Control copy_selected(void*, std::size_t) const noexcept;
  void destroy_selected() const noexcept;
  std::size_t metadata_bytes() const noexcept;
};
using Realization = orbital::Representation<Control>;
using Export = orbital::Part<Control>;
using orbital::retain;
using orbital::release;
using orbital::view;
using orbital::binding;
struct Specification { Shape* shape{}; std::uint64_t records{}; Owners owners = Owners::independent_shared; };
// Allocation profiles reserve metadata and validate all sources before taking
// their existing physical uses. Shared profiles copy supplied owners. Writable
// aliases end before capture; no Object/version label is a physical authority.
[[nodiscard]] Result<Realization*> capture(std::pmr::memory_resource&,
    const Specification&, std::span<const BodySource>) noexcept;
std::uint64_t records(const Realization*) noexcept;
std::pmr::memory_resource* resource(const Realization*) noexcept;
// Conversion compatibility only: equal semantic ProgramId and record count.
// The caller must establish equal logical content before catalog adoption;
// this predicate neither compares bytes nor proves a lossless conversion.
bool equivalent(const Realization*, const Realization*) noexcept;

// The native physical borrow is Orbital's; these two conveniences interpret
// its static caller Control without adding state or another ownership layer.
struct Selection : orbital::RepresentationSelection<Control> {
  std::uint64_t records() const noexcept;
  const Shape* shape() const noexcept;
};

namespace detail {
// Internal fixture forwarding only. The catalog's sealed BoundSelection supplies
// this association and exposes export_part(); callers cannot choose its labels.
[[nodiscard]] Result<Export*> retain_part(Selection, std::size_t, Binding,
    Interpretation = Interpretation::shared_control) noexcept;
} // namespace detail
std::uint64_t records(const Export*) noexcept;
const Geometry& geometry(const Export*) noexcept;
const ReadLaw& tuple_law(const Export*) noexcept;
Bytes tuple_interpretation(const Export*) noexcept;
[[nodiscard]] Result<fixture::sp::descriptor_bytes> series_interpretation(const Export*) noexcept;

struct Footprint { std::size_t requested_bytes{}, copied_metadata{}, shared_shape_bytes{}, owner_bytes{}; };
Footprint footprint(const Realization*) noexcept;
Footprint footprint(const Export*) noexcept;
std::size_t shape_bytes(const Shape*) noexcept;

static_assert(sizeof(Geometry) == 32);
static_assert(sizeof(View) == 8 && sizeof(Result<View>) == 16);
static_assert(sizeof(Selection) == 8 && sizeof(Result<Selection>) == 16);
static_assert(sizeof(Control) == 16);
static_assert(std::is_trivially_copyable_v<View> && std::is_trivially_copyable_v<Selection> &&
    std::is_trivially_copyable_v<Control>);

} // namespace compact_shape

extern template struct orbital::RepresentationSelection<compact_shape::Control>;
