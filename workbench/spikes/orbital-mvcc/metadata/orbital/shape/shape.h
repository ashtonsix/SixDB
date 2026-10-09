#pragma once

#include "../descriptor/descriptor.h"
#include "../../consumer/compact_law.h"
#include <orbital/allocation.h>

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
struct Realization;
struct Export;

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

enum class Owners { independent_shared, grouped_shared, independent_allocation, grouped_allocation };
struct Specification { Shape* shape{}; std::uint64_t records{}; Owners owners = Owners::independent_shared; };
struct BodySource {
  PartId id{};
  Bytes body;
  std::shared_ptr<const void> backing;
  orbital::allocation::Allocation* allocation{};
};

// Allocation profiles establish full-range Holds, then release the supplied
// Allocations only on success. Shared profiles copy supplied owners. Writable
// aliases end before capture; no Object/version label is a physical authority.
[[nodiscard]] Result<Realization*> capture(std::pmr::memory_resource&,
    const Specification&, std::span<const BodySource>) noexcept;
void retain(Realization*) noexcept;
void release(Realization*) noexcept;
std::uint64_t records(const Realization*) noexcept;
std::pmr::memory_resource* resource(const Realization*) noexcept;
bool equivalent(const Realization*, const Realization*) noexcept;

// Pointer-only native Part borrow. Count and proven geometry stay with its
// Selection/control; an empty Part still has a valid address slot.
struct View {
  const std::byte* const* slot{};
  explicit operator bool() const noexcept { return slot; }
  const std::byte* address() const noexcept { return *slot; }
};
struct Control { const Shape* shape; std::uint64_t records; };
struct Selection {
  Realization* realization{};
  explicit operator bool() const noexcept { return realization; }
  std::uint64_t records() const noexcept;
  const Shape* shape() const noexcept;
  Control control() const noexcept;
  std::size_t part_count() const noexcept;
  [[nodiscard]] Result<View> part(std::size_t) const noexcept;
  [[nodiscard]] Result<View> part(PartId) const noexcept;
};

// This snapshot is created by the catalog's cold exact-version binding path;
// it is not a capture input or a publication capability.
struct Binding { ObjectId object{}; std::uint64_t version{}; };
enum class Interpretation { shared_shape, selected_copy };
namespace detail {
// Internal fixture forwarding only. The catalog's sealed BoundSelection supplies
// this association and exposes export_part(); callers cannot choose its labels.
[[nodiscard]] Result<Export*> retain_part(Selection, std::size_t, Binding,
    Interpretation = Interpretation::shared_shape) noexcept;
} // namespace detail
void retain(Export*) noexcept;
void release(Export*) noexcept;
View view(const Export*) noexcept;
Binding binding(const Export*) noexcept;
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
