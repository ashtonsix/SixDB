#include "material.h"
#include "kernels.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
#include <cassert>
#include <limits>

namespace frame_material_witness {
bool Control::valid(std::pmr::memory_resource& resource) const noexcept {
    if (memory != &resource || first >= 2 || !count || count > 2 - first ||
        (owners != orbital::PartOwners::independent_allocation &&
         owners != orbital::PartOwners::grouped_allocation)) return false;
    for (unsigned i = 0; i < count; ++i) {
        const auto& r = region(i);
        if (!r.count || r.count > 65536 || !r.width || r.width > 64 ||
            r.first_position > UINT32_MAX - r.count) return false;
        const auto maximum = r.width == 64 ? UINT64_MAX : (UINT64_C(1) << r.width) - 1;
        if (r.base > UINT64_MAX - maximum) return false;
    }
    return true;
}
void Physical::retain(Value* value) noexcept { orbital::retain(value); }
void Physical::release(Value* value) noexcept { orbital::release(value); }
orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& memory,
    const Control& control, std::span<const PartSource> parts) noexcept {
    return orbital::capture_representation(memory, control, control.owners, parts);
}
bool Physical::accepts_resource(Value* value, std::pmr::memory_resource& memory) noexcept {
    return value && Selection{value}.control().resource() == &memory;
}
bool Physical::compatible(Value* left, Value* right) noexcept {
    const auto a = Selection{left}.control(), b = Selection{right}.control();
    if (a.first != b.first || a.count != b.count) return false;
    for (unsigned i = 0; i < a.count; ++i)
        if (a.region(i).count != b.region(i).count ||
            a.region(i).first_position != b.region(i).first_position) return false;
    return true; // Same coordinate domain; does not certify logical equivalence.
}
Physical::Footprint Physical::footprint(Value* value) noexcept {
    const auto control = Selection{value}.control();
    return {orbital::object_detail::representation_bytes(control, control.owners), 0};
}
orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection selected,
    std::size_t index, orbital::ObjectBinding binding, ExportPolicy policy) noexcept {
    return orbital::object_detail::retain_part(selected, index, binding, policy);
}

std::uint64_t point(const Region& r, orbital::View view, unsigned row) {
    assert(view && row < r.count);
    return r.base + frame_encoding::plane_kernels(r.width).point(
        reinterpret_cast<const std::uint8_t*>(view.address()), row);
}
void range(const Region& r, orbital::View view, unsigned first, std::span<std::uint64_t> out) {
    assert(view && first <= r.count && out.size() <= r.count - first);
    frame_encoding::plane_kernels(r.width).range(
        reinterpret_cast<const std::uint8_t*>(view.address()), first, out.size(), out.data());
    for (auto& value : out) value += r.base;
}
} // namespace frame_material_witness

template struct orbital::object_detail::History<frame_material_witness::Physical>;
template class orbital::ObjectStore<frame_material_witness::Physical>;
template struct orbital::RepresentationSelection<frame_material_witness::Control>;
template void orbital::release(orbital::Part<frame_material_witness::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<frame_material_witness::Control>*) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<frame_material_witness::Control>*) noexcept;
template orbital::Result<orbital::PartSource> orbital::source(
    frame_material_witness::Selection, std::size_t) noexcept;
