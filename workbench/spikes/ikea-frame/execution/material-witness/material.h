#pragma once
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>
#include <span>
#include <type_traits>

// Private lifetime witness, not a Frame format. Each region is an actual Local
// SeriesPack FOR plane with its interpretation copied inline, independently of
// the column/vector object that originally produced its values.
namespace frame_material_witness {
struct Region {
    std::uint64_t base{};
    unsigned count{}, width{}, first_position{}, readable_padding{};
    std::size_t bytes() const noexcept { return (std::size_t(count) + 7) / 8 * width; }
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

// The caller already selected a coherent Region and its certified byte view.
std::uint64_t point(const Region&, orbital::View, unsigned row);
void range(const Region&, orbital::View, unsigned first, std::span<std::uint64_t> out);
} // namespace frame_material_witness

extern template class orbital::ObjectStore<frame_material_witness::Physical>;
extern template struct orbital::RepresentationSelection<frame_material_witness::Control>;
extern template void orbital::release(orbital::Part<frame_material_witness::Control>*) noexcept;
extern template orbital::View orbital::view(const orbital::Part<frame_material_witness::Control>*) noexcept;
extern template orbital::ObjectBinding orbital::binding(const orbital::Part<frame_material_witness::Control>*) noexcept;
extern template orbital::Result<orbital::PartSource> orbital::source(
    frame_material_witness::Selection, std::size_t) noexcept;
