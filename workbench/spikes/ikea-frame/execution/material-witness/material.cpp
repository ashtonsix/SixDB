#include "material.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
#include <cassert>

namespace frame_material_witness {
bool Region::valid() const noexcept {
    if (!block_count || block_count > region_blocks ||
        first_position > UINT32_MAX - rows() ||
        unsigned(checkpoints) > unsigned(fe::checkpoints::none)) return false;
    const auto fits = [&](std::uint32_t offset, std::size_t bytes) {
        return offset <= body_bytes && bytes <= body_bytes - offset;
    };
    const auto& d = dictionary;
    if (d.count && (!d.edition.scope || !d.width || d.width > 64 ||
        (d.order != fe::dictionary_order::sorted && d.order != fe::dictionary_order::frequency) ||
        d.grant_bytes < d.bytes() || !fits(d.offset, d.grant_bytes))) return false;
    for (unsigned i = 0; i < block_count; ++i) {
        const auto& b = blocks[i];
        if (unsigned(b.descriptor.type) > unsigned(fe::kind::delta_patched) ||
            b.rows != fe::block_rows || b.grant_bytes < b.body_bytes ||
            !fits(b.offset, b.grant_bytes) ||
            (b.coded && (!d.count || b.dictionary_edition != d.edition))) return false;
    }
    return true;
}
bool Control::valid(std::pmr::memory_resource& resource) const noexcept {
    if (memory != &resource || first >= 2 || !count || count > 2 - first ||
        (owners != orbital::PartOwners::independent_allocation &&
         owners != orbital::PartOwners::grouped_allocation)) return false;
    for (unsigned i = 0; i < count; ++i) if (!region(i).valid()) return false;
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
        if (a.region(i).rows() != b.region(i).rows() ||
            a.region(i).first_position != b.region(i).first_position) return false;
    return true; // Coordinates, not a proof of logical equivalence.
}
Physical::Footprint Physical::footprint(Value* value) noexcept {
    const auto control = Selection{value}.control();
    return {orbital::object_detail::representation_bytes(control, control.owners), 0};
}
orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection selected,
    std::size_t index, orbital::ObjectBinding binding, ExportPolicy policy) noexcept {
    return orbital::object_detail::retain_part(selected, index, binding, policy);
}

fe::borrowed_block BorrowedRegion::block(unsigned index) const {
    assert(index < interpretation->block_count);
    const auto& b = interpretation->blocks[index];
    fe::borrowed_block k{};
    k.d = b.descriptor;
    k.body = reinterpret_cast<const std::uint8_t*>(material.data()) + b.offset;
    k.grant = b.grant_bytes;
    k.delta_checkpoints = interpretation->checkpoints;
    k.rows = b.rows;
    k.coded = b.coded;
    if (b.coded) {
        const auto& d = interpretation->dictionary;
        k.dictionary = {d.min, d.count, d.width, d.order,
            reinterpret_cast<const std::uint8_t*>(material.data()) + d.offset,
            d.grant_bytes, d.edition};
    }
    return k;
}
std::optional<BorrowedRegion> attach(const Region& r, orbital::Bytes bytes) {
    if (!r.valid() || bytes.size() != r.bytes()) return {};
    BorrowedRegion view{&r, bytes};
    for (unsigned i = 0; i < r.block_count; ++i) {
        const auto k = view.block(i);
        const auto extent = fe::extent_of(k.d, k.delta_checkpoints, k.body, k.grant);
        if (!extent || extent->body_bytes != r.blocks[i].body_bytes || !fe::admits(k)) return {};
    }
    return view;
}
std::uint64_t BorrowedRegion::point(unsigned row) const {
    assert(row < interpretation->rows());
    return fe::value_at(block(row / fe::block_rows), row % fe::block_rows);
}
void BorrowedRegion::range(unsigned first, std::span<std::uint64_t> out) const {
    assert(first <= interpretation->rows() && out.size() <= interpretation->rows() - first);
    while (!out.empty()) {
        const auto local = first % fe::block_rows;
        const auto take = std::min<std::size_t>(out.size(), fe::block_rows - local);
        const auto k = block(first / fe::block_rows);
        if (!local && take == fe::block_rows) fe::decode_block(k, out.data());
        else for (unsigned i = 0; i < take; ++i) out[i] = fe::value_at(k, local + i);
        first += unsigned(take); out = out.subspan(take);
    }
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
