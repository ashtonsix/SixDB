#pragma once
#include "../../encoding/material.h"
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>
#include <optional>
#include <type_traits>

// Private lifetime fixture, not a Frame format. Actual encoded bodies and an
// optional dictionary live in each native Part. Interpretation is pointer-free;
// only the attached, scope-bounded reader contains byte addresses.
namespace frame_material_witness {
namespace fe = frame_encoding;
constexpr unsigned region_blocks = 16;
struct Block {
    fe::descriptor descriptor{};
    std::uint32_t offset{}, body_bytes{}, grant_bytes{}, rows = fe::block_rows;
    bool coded{};
    fe::dictionary_identity dictionary_edition{};
};
struct Dictionary {
    std::uint64_t min{};
    fe::dictionary_identity edition{};
    std::uint32_t offset{}, count{}, grant_bytes{};
    std::uint8_t width{};
    fe::dictionary_order order = fe::dictionary_order::none;
    std::size_t bytes() const noexcept { return count ? fe::plane_bytes(width, count) : 0; }
};
struct Region {
    std::array<Block, region_blocks> blocks{};
    Dictionary dictionary{};
    unsigned block_count{}, first_position{}, body_bytes{}, readable_padding{};
    fe::checkpoints checkpoints = fe::checkpoints::native;
    unsigned rows() const noexcept { return block_count * fe::block_rows; }
    std::size_t bytes() const noexcept { return body_bytes; }
    bool valid() const noexcept;
};
struct Control {
    std::pmr::memory_resource* memory{};
    std::array<Region, 2> regions{};
    unsigned first{}, count{2};
    orbital::PartOwners owners{orbital::PartOwners::independent_allocation};
    bool valid(std::pmr::memory_resource&) const noexcept;
    auto* resource() const noexcept { return memory; }
    const Region& region(std::size_t i) const noexcept { return regions[first + i]; }
    std::size_t part_count() const noexcept { return count; }
    orbital::PartId id(std::size_t i) const noexcept { return {0x4652414d45, std::uint32_t(first + i)}; }
    orbital::Result<std::size_t> extent(std::size_t i) const noexcept { return region(i).bytes(); }
    std::uint32_t padding(std::size_t i) const noexcept { return region(i).readable_padding; }
    std::size_t alignment(std::size_t) const noexcept { return 64; }
    void retain() const noexcept {}
    void release() const noexcept {}
    std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
    Control copy_selected(void*, std::size_t i) const noexcept {
        Control next;
        next.memory = memory;
        next.first = first + unsigned(i);
        next.count = 1;
        next.regions[next.first] = region(i);
        return next;
    }
    std::size_t metadata_bytes() const noexcept { return 0; }
    void destroy_selected() const noexcept {}
};
static_assert(std::is_trivially_copyable_v<Control>);

using Selection = orbital::RepresentationSelection<Control>;
struct Physical {
    using Value = orbital::Representation<Control>;
    using Selection = frame_material_witness::Selection;
    using Specification = Control;
    using PartSource = orbital::PartSource;
    using ExportPolicy = orbital::PartRetention;
    struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
    static void retain(Value*) noexcept;
    static void release(Value*) noexcept;
    static Value* get(Selection s) noexcept { return s.realization; }
    static Selection selection(Value* p) noexcept { return {p}; }
    // The owner admits initialized Frame contents before calling capture.
    // Orbital validates and acquires physical sources; this does not parse an
    // arbitrary source span before its ownership certificate is validated.
    static orbital::Result<Value*> capture(std::pmr::memory_resource&, const Control&,
                                         std::span<const PartSource>) noexcept;
    static bool accepts(Value*, orbital::ObjectId, std::uint64_t) noexcept { return true; }
    static bool accepts_resource(Value*, std::pmr::memory_resource&) noexcept;
    static bool compatible(Value*, Value*) noexcept;
    static Footprint footprint(Value*) noexcept;
    static orbital::Result<orbital::Part<Control>*> export_part(Selection, std::size_t,
                                       orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;

struct BorrowedRegion {
    const Region* interpretation;
    orbital::Bytes material;
    fe::borrowed_block block(unsigned index) const;
    std::uint64_t point(unsigned row) const;
    void range(unsigned first, std::span<std::uint64_t> out) const;
};
// Bytes are an actual initialized Allocation or certified Part body. Admit once
// before capture/consumption; neither failure nor success owns or consumes them.
std::optional<BorrowedRegion> attach(const Region&, orbital::Bytes);
std::optional<BorrowedRegion> attach(const Region&&, orbital::Bytes) = delete;
} // namespace frame_material_witness

extern template class orbital::ObjectStore<frame_material_witness::Physical>;
extern template struct orbital::RepresentationSelection<frame_material_witness::Control>;
extern template void orbital::release(orbital::Part<frame_material_witness::Control>*) noexcept;
extern template orbital::View orbital::view(const orbital::Part<frame_material_witness::Control>*) noexcept;
extern template orbital::ObjectBinding orbital::binding(const orbital::Part<frame_material_witness::Control>*) noexcept;
extern template orbital::Result<orbital::PartSource> orbital::source(
    frame_material_witness::Selection, std::size_t) noexcept;
