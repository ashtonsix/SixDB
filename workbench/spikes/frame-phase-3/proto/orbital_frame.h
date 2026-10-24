// Prototype 6: frame parts as Orbital Objects (../README.md, ideas 3 and 4).
//
// One Object per column part; its Representation has one Part per group,
// each Part a headless group image (stage::emit). Readers attach a stage
// tree to the Parts of the version their Scope selects. A write copies only
// its group's image into a draft, applies there, and publishes a new
// Representation that reuses every other group's Part.
#pragma once
#include <orbital/object.h>
#include <orbital/representation.h>

#include <cstdint>
#include <memory_resource>
#include <type_traits>
#include <vector>

namespace frame3_orbital {
/// Interpretation a Representation retains: its Parts' extents. Kept alive
/// by the driver for the prototype; uses count Control retains.
struct Geometry {
    std::pmr::memory_resource *memory{};
    std::uint64_t object{};
    std::vector<std::size_t> extents;
    unsigned uses = 0;
};
struct Control {
    Geometry *geometry{};
    bool valid(std::pmr::memory_resource &resource) const noexcept { return geometry && geometry->memory == &resource; }
    auto *resource() const noexcept { return geometry->memory; }
    std::size_t part_count() const noexcept { return geometry->extents.size(); }
    orbital::PartId id(std::size_t index) const noexcept { return {geometry->object, index}; }
    orbital::Result<std::size_t> extent(std::size_t index) const noexcept { return geometry->extents[index]; }
    std::uint32_t padding(std::size_t) const noexcept { return 0; }
    std::size_t alignment(std::size_t) const noexcept { return 1; }
    void retain() const noexcept { ++geometry->uses; }
    void release() const noexcept { --geometry->uses; }
    std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
    Control copy_selected(void *, std::size_t) const noexcept { return *this; }
    std::size_t metadata_bytes() const noexcept { return 0; }
    void destroy_selected() const noexcept {}
};
static_assert(std::is_trivially_copyable_v<Control>);

using Selection = orbital::RepresentationSelection<Control>;
struct Physical {
    using Value = orbital::Representation<Control>;
    using Selection = frame3_orbital::Selection;
    using Specification = Control;
    using PartSource = orbital::PartSource;
    using ExportPolicy = orbital::PartRetention;
    struct Footprint {
        std::size_t requested_bytes{}, copied_metadata{};
    };
    static void retain(Value *) noexcept;
    static void release(Value *) noexcept;
    static Value *get(Selection selected) noexcept { return selected.realization; }
    static Selection selection(Value *value) noexcept { return {value}; }
    static orbital::Result<Value *> capture(std::pmr::memory_resource &, const Control &, std::span<const PartSource>) noexcept;
    static bool accepts(Value *, orbital::ObjectId, std::uint64_t) noexcept { return true; }
    static bool accepts_resource(Value *, std::pmr::memory_resource &) noexcept;
    static bool compatible(Value *, Value *) noexcept;
    static Footprint footprint(Value *) noexcept;
    static orbital::Result<orbital::Part<Control> *> export_part(Selection, std::size_t, orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;
/// A retained Part of an earlier Representation, for reuse without copying.
orbital::Result<orbital::PartSource> reuse(Selection, std::size_t) noexcept;
}  // namespace frame3_orbital

extern template class orbital::ObjectStore<frame3_orbital::Physical>;
extern template struct orbital::RepresentationSelection<frame3_orbital::Control>;
