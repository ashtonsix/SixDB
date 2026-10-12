#pragma once
#include "grouped.h"
#include <orbital/object.h>
#include <orbital/representation.h>
#include <orbital/detail/object/representation.h>
#include <array>
#include <vector>

// One deliberately small *caller*, using real Orbital ownership/publication.
// It is not an allocation facade or a proposed StreamPack container API.
namespace streampack_material {
namespace sn = streampack_next;
namespace oa = orbital::allocation;
using orbital::Result;
using orbital::Error;
constexpr std::uint32_t absent = ~std::uint32_t{};
constexpr std::uint32_t location(unsigned block, unsigned rank) { return (block << 16) | rank; }
constexpr unsigned block_of(std::uint32_t x) { return x >> 16; }
constexpr unsigned rank_of(std::uint32_t x) { return x & 65535; }
struct Summary {
  std::uint32_t live_rows{}, used_bytes{};
  std::uint8_t minimum{255}, maximum{};
  bool operator==(const Summary&) const = default;
};
enum class Geometry : std::uint8_t { direct32, widths8 };
struct Layout {
  unsigned capacity;
  Geometry geometry{Geometry::direct32};
  std::size_t ends() const { return 8; }
  std::size_t bases() const { return ends() + capacity; } // widths8 only; capacity is a multiple of 128
  std::size_t used() const { return geometry == Geometry::direct32 ? ends() + (capacity + 1) * 4 : bases() + (capacity / 16 + 1) * 4; }
  std::size_t tombs() const { return (used() + capacity * 2 + 7) & ~std::size_t(7); }
  std::size_t bytes() const { return tombs() + capacity / 8; }
};
struct Control {
  std::pmr::memory_resource* memory{};
  std::uint32_t body_extent{};
  std::uint16_t capacity{256}, holders{};
  Summary summary;
  // A selected-copy export retains this inline geometry, selecting one Part.
  std::uint8_t selected{255};
  std::uint8_t blocks{1}; // bounded concrete caller, not an extensible container
  std::uint32_t second_body_extent{};
  std::uint16_t holder_capacity{}; // explicit when logical and physical extents differ
  Geometry geometry{Geometry::direct32}; // cold material choice, never a per-rank dispatch
  unsigned maximum() const noexcept { return geometry == Geometry::widths8 ? 255 : 65535; }
  unsigned holder_limit() const noexcept { return holder_capacity ? holder_capacity : capacity * blocks; }
  std::size_t full_parts() const noexcept { return 2 * blocks + 1; }
  std::size_t holder_part() const noexcept { return 2 * blocks; }
  std::uint32_t body_bytes(unsigned b) const noexcept { return b ? second_body_extent : body_extent; }
  bool valid(std::pmr::memory_resource& r) const noexcept {
    return memory == &r && (blocks == 1 || blocks == 2) && (capacity == 128 || capacity == 256 || capacity == 512) &&
        holders <= holder_limit() && holder_limit() <= 1024 &&
        (geometry == Geometry::direct32 || geometry == Geometry::widths8) &&
        body_extent <= std::uint32_t(capacity) * maximum() && second_body_extent <= std::uint32_t(capacity) * maximum() &&
        (blocks == 2 || !second_body_extent) && (selected == 255 || selected < full_parts());
  }
  auto* resource() const noexcept { return memory; }
  std::size_t part_count() const noexcept { return selected == 255 ? full_parts() : 1; }
  unsigned original(std::size_t i) const noexcept { return selected == 255 ? unsigned(i) : selected; }
  orbital::PartId id(std::size_t i) const noexcept { return {0x53504b, original(i)}; }
  Result<std::size_t> extent(std::size_t i) const noexcept {
    const auto part = original(i);
    if (part == holder_part()) return holder_limit() * sizeof(std::uint32_t);
    if (part >= holder_part()) return std::unexpected(Error::invalid_argument);
    return part % 2 ? Layout{capacity, geometry}.bytes() : body_bytes(part / 2);
  }
  std::size_t alignment(std::size_t i) const noexcept { return original(i) < holder_part() && original(i) % 2 == 0 ? 1 : 8; }
  std::uint32_t padding(std::size_t) const noexcept { return 0; }
  void retain() const noexcept {}
  void release() const noexcept {}
  std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
  Control copy_selected(void*, std::size_t i) const noexcept { auto c = *this; c.selected = original(i); return c; }
  std::size_t metadata_bytes() const noexcept { return 0; }
  void destroy_selected() const noexcept {}
};
using Selection = orbital::RepresentationSelection<Control>;
struct Physical {
  using Value = orbital::Representation<Control>;
  using Selection = streampack_material::Selection;
  using Specification = Control;
  using PartSource = orbital::PartSource;
  using ExportPolicy = orbital::PartRetention;
  struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
  static void retain(Value* v) noexcept { orbital::retain(v); }
  static void release(Value* v) noexcept { orbital::release(v); }
  static Value* get(Selection s) noexcept { return s.realization; }
  static Selection selection(Value* v) noexcept { return {v}; }
  static Result<Value*> capture(std::pmr::memory_resource&, const Control&, std::span<const PartSource>) noexcept;
  static bool accepts(Value*, orbital::ObjectId, std::uint64_t) noexcept { return true; }
  static bool accepts_resource(Value* v, std::pmr::memory_resource& r) noexcept { return v->control.resource() == &r; }
  static bool compatible(Value* a, Value* b) noexcept { return a->control.holders == b->control.holders; }
  static Footprint footprint(Value* v) noexcept { return {sizeof(Value) + v->control.part_count() * 16, 0}; }
  static Result<orbital::Part<Control>*> export_part(Selection, std::size_t, orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;
sn::Direct<std::uint32_t, true, true> read(Selection, unsigned block = 0);
sn::Grouped<std::uint8_t, false, true, true> read_widths(Selection, unsigned block = 0);
std::span<const std::uint32_t> holders(Selection);
Summary summarize(Selection);

struct HolderView { std::span<std::uint32_t> rows; };
struct Draft {
  Control control;
  std::array<oa::Allocation, 5> parts;
  Store::Owner predecessor; // actual clone basis, retained through damage capture
  static Result<Draft> create(oa::Pool&, Control, std::size_t body_capacity);
  static Result<Draft> create(oa::Pool&, Control, std::array<std::size_t, 2> body_capacities);
  static Result<Draft> clone(oa::Pool&, Selection, std::size_t body_capacity);
  static Result<Draft> clone(oa::Pool&, Selection, std::array<std::size_t, 2> body_capacities);
  sn::MutableDirect<std::uint32_t> write(unsigned block = 0);
  sn::MutableWidths<std::uint8_t> write_widths(unsigned block = 0);
  HolderView holder_view();
  std::vector<orbital::PartSource> sources();
  void finish(); // final extent and exact fixture summary, before capture
};

struct Patch { unsigned part; std::size_t offset; std::vector<std::byte> postimage; };
struct Damage {
  Store::Owner predecessor; // exact physical interpretation, not just a cut
  Control target;
  std::vector<Patch> patches; // owner output after stores, never inside before()
};
// Resolve while the named source objects live, then own stable afterimages.
Result<Damage> freeze_damage(Selection predecessor, Draft& completed,
    const sn::MutableDirect<std::uint32_t>& source, const HolderView&,
    std::span<const ikea::owner_write> issued);
Result<Damage> freeze_damage(Selection predecessor, Draft& completed,
    std::span<const sn::MutableDirect<std::uint32_t>> sources, const HolderView&,
    std::span<const ikea::owner_write> issued);
Result<Damage> freeze_damage(Selection predecessor, Draft& completed,
    const sn::MutableWidths<std::uint8_t>& source, const HolderView&,
    std::span<const ikea::owner_write> issued);
Result<Damage> freeze_damage(Selection predecessor, Draft& completed,
    std::span<const sn::MutableWidths<std::uint8_t>> sources, const HolderView&,
    std::span<const ikea::owner_write> issued);
// This consumer actually uses damage to reconstruct MVCC publication material.
// The whole-copy predecessor is an explicit initial correctness control.
Result<Draft> reconstruct(oa::Pool&, Selection exact_predecessor, const Damage&);
Result<Draft> reconstruct(oa::Pool&, Selection exact_predecessor, const Damage&,
    std::array<std::size_t, 2> body_capacities);
} // namespace streampack_material
extern template class orbital::ObjectStore<streampack_material::Physical>;
