#include "implementation_orbital.h"
#include <orbital/detail/object/representation.h>
#include <orbital/instantiate/representation.h>
#include <orbital/instantiate/object.h>
#include <algorithm>
#include <cstring>

namespace streampack_implementation::orbital_material {
namespace {
std::size_t round64(std::size_t n) { return (n + 63) & ~std::size_t(63); }
sp::storage<const sp::byte> windows(Selection s) {
    const auto c = s.control();
    std::array<sp::bytes, 4> parts;
    for (unsigned p = 0; p < 4; ++p) parts[p] = {s.part(p)->address(), c.extents[p]};
    return {parts[0], parts[1], parts[2], parts[3]};
}
}

std::expected<FormatOwner, sp::error> prepare_format(sp::description d) {
    auto layout = sp::layout::prepare(d);
    if (!layout) return std::unexpected(layout.error());
    return FormatOwner::retain(*new Format{*layout});
}

bool Control::valid(std::pmr::memory_resource& r) const noexcept {
    return memory == &r && format && selected <= 5 &&
        (placement == Placement::grouped || placement == Placement::separate) &&
        extents[0] <= format->layout.schema().byte_limit &&
        extents[1] == format->layout.directory_bytes() &&
        extents[2] <= format->layout.lengths_bytes() &&
        extents[3] == format->layout.control_bytes() &&
        extents[4] == format->layout.capacity() * sizeof(std::uint32_t);
}

orbital::Result<std::size_t> Control::extent(std::size_t i) const noexcept {
    if (i >= part_count()) return std::unexpected(orbital::Error::invalid_argument);
    return extents[original(i)];
}

std::size_t Control::alignment(std::size_t i) const noexcept {
    const auto p = original(i);
    return p == 2 ? format->layout.lengths_alignment() : p == 4 ? alignof(std::uint32_t) : 1;
}

Control Control::copy_selected(void*, std::size_t i) const noexcept {
    auto result = *this;
    result.selected = original(i);
    result.retain();
    return result;
}

orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& r,
    const Control& c, std::span<const PartSource> parts) noexcept {
    return orbital::capture_representation(r, c, c.placement == Placement::grouped ?
        orbital::PartOwners::grouped_allocation : orbital::PartOwners::independent_allocation, parts);
}

bool Physical::accepts_resource(Value* v, std::pmr::memory_resource& r) noexcept {
    return v->control.resource() == &r;
}

bool Physical::compatible(Value* a, Value* b) noexcept {
    return a->control.format == b->control.format;
}

Physical::Footprint Physical::footprint(Value* v) noexcept {
    return {orbital::object_detail::representation_bytes(v->control, v->owners), 0};
}

orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection s, std::size_t i,
    orbital::ObjectBinding b, ExportPolicy policy) noexcept {
    return orbital::object_detail::retain_part(s, i, b, policy);
}

sp::const_view read(Selection s) {
    assert(s && s.control().selected == 5);
    return sp::const_view::assume_valid(s.control().format->layout, windows(s));
}

std::span<const std::uint32_t> holders(Selection s) {
    assert(s && s.control().selected == 5);
    return {reinterpret_cast<const std::uint32_t*>(s.part(4)->address()),
        s.control().format->layout.capacity()};
}

std::byte* Draft::address(unsigned p) const noexcept {
    return placement == Placement::grouped ? allocations[0].address + offsets[p] : allocations[p].address;
}

orbital::Result<void> Draft::rebind() {
    std::array<std::span<sp::byte>, 4> planes;
    for (unsigned p = 0; p < 4; ++p) planes[p] = {address(p), extents[p]};
    auto result = sp::view::bind(format->layout, {planes[0], planes[1], planes[2], planes[3]});
    if (!result) return std::unexpected(orbital::Error::invalid_argument);
    source = *result;
    return {};
}

orbital::Result<Draft> Draft::create(loom::Worker& worker, FormatOwner f,
    std::pmr::memory_resource& memory, std::size_t payload_bytes, Placement placement,
    unsigned length_slots) {
    if (!f || payload_bytes > f->layout.schema().byte_limit || length_slots > f->layout.capacity() ||
        (placement != Placement::grouped && placement != Placement::separate))
        return std::unexpected(orbital::Error::invalid_argument);
    Draft d;
    d.format = std::move(f); d.memory = &memory; d.pool = worker.memory; d.placement = placement;
    const auto& l = d.format->layout;
    d.extents = {payload_bytes, l.directory_bytes(),
        l.lengths_bytes(length_slots ? length_slots : l.capacity()), l.control_bytes(),
        l.capacity() * sizeof(std::uint32_t)};
    if (placement == Placement::grouped) {
        // Payload comes last physically, so its growth cannot shift metadata.
        std::size_t total = 0;
        for (auto p : {1u, 2u, 3u, 4u, 0u}) {
            total = round64(total); d.offsets[p] = total; total += d.extents[p];
        }
        auto allocation = loom::allocate(worker, {total, 0, 64, 0, oa::Initialization::zero});
        if (!allocation) return std::unexpected(allocation.error());
        d.allocations[0] = std::move(*allocation);
    } else {
        for (unsigned p = 0; p < 5; ++p) {
            if (!d.extents[p]) continue;
            auto allocation = loom::allocate(worker, {d.extents[p], 0, 64, 0, oa::Initialization::zero});
            if (!allocation) return std::unexpected(allocation.error());
            d.allocations[p] = std::move(*allocation);
        }
    }
    auto bound = d.rebind();
    if (!bound) return std::unexpected(bound.error());
    ikea::no_coverage effects;
    if (!sp::initialize(d.write(), effects)) return std::unexpected(orbital::Error::invalid_argument);
    std::fill(d.holders().begin(), d.holders().end(), absent);
    return d;
}

orbital::Result<Draft> Draft::clone(loom::Worker& worker, Selection s,
    std::size_t payload_bytes, Placement placement) {
    if (!s || s.control().selected != 5) return std::unexpected(orbital::Error::invalid_argument);
    const auto c = s.control();
    auto f = FormatOwner::retain(*c.format);
    auto d = create(worker, std::move(f), *c.memory, std::max(payload_bytes, c.extents[0]), placement);
    if (!d) return d;
    for (unsigned p = 0; p < 5; ++p) {
        if (c.extents[p]) std::memcpy(d->address(p), s.part(p)->address(), c.extents[p]);
        d->copied_bytes += c.extents[p];
    }
    return d;
}

std::span<std::uint32_t> Draft::holders() noexcept {
    return {reinterpret_cast<std::uint32_t*>(address(4)), format->layout.capacity()};
}

orbital::Result<void> Draft::grow(std::size_t payload_bytes, unsigned length_slots) {
    if (!source) return std::unexpected(orbital::Error::invalid_argument);
    const auto& l = format->layout;
    if (payload_bytes < extents[0] || payload_bytes > l.schema().byte_limit ||
        length_slots > l.capacity() || l.lengths_bytes(length_slots) < extents[2])
        return std::unexpected(orbital::Error::invalid_argument);
    const auto lengths = l.lengths_bytes(length_slots);
    if (placement == Placement::grouped) {
        if (lengths != extents[2]) return std::unexpected(orbital::Error::invalid_argument);
        auto& a = allocations[0];
        const auto before = a.size;
        auto result = oa::grow(a, {offsets[0] + payload_bytes, 0, 64});
        if (!result) return result;
        if (a.size > before) std::memset(a.address + before, 0, a.size - before);
        extents[0] = payload_bytes;
    } else {
        // Two independently atomic growth operations; callers observe an enlarged
        // payload if the later lengths growth refuses. Encoded contents stay valid.
        for (auto p : {0u, 2u}) {
            const auto wanted = p == 0 ? payload_bytes : lengths;
            if (wanted == extents[p]) continue;
            if (!allocations[p]) {
                auto made = oa::allocate(*pool, {wanted, 0, 64, 0, oa::Initialization::zero});
                if (!made) { (void)rebind(); return std::unexpected(made.error()); }
                allocations[p] = std::move(*made); extents[p] = wanted;
                continue;
            }
            auto& a = allocations[p]; const auto before = a.size;
            auto result = oa::grow(a, {wanted, 0, 64});
            if (!result) { (void)rebind(); return result; }
            if (a.size > before) std::memset(a.address + before, 0, a.size - before);
            extents[p] = wanted;
        }
    }
    return rebind();
}

orbital::Result<Store::Owner> Draft::capture(Store& store) {
    if (!source) return std::unexpected(orbital::Error::invalid_argument);
    if (!write().validate()) return std::unexpected(orbital::Error::invalid_argument);
    const auto n = write().size();
    for (auto rank : holders()) {
        if (rank != absent && (rank >= n || !write().live_unchecked(rank)))
            return std::unexpected(orbital::Error::invalid_argument);
    }
    auto captured = extents;
    captured[0] = write().extent();
    captured[2] = format->layout.lengths_bytes(n);
    const Control control{memory, format.get(), captured, placement};
    std::array<orbital::PartSource, 5> sources;
    for (unsigned p = 0; p < 5; ++p)
        sources[p] = {control.id(p), {address(p), captured[p]}, {},
            &allocations[placement == Placement::grouped ? 0 : p]};
    auto owner = store.capture(control, sources);
    if (owner) source.reset();
    return owner;
}

orbital::Result<void> publish(Store& store, const Store::Scope& basis,
    orbital::ObjectId id, const Store::Owner& owner) {
    const std::array<Store::Change, 1> changes{{{id, owner}}};
    Store::Prepared prepared(store);
    auto accepted = store.prepare(basis, changes, prepared);
    if (!accepted) return accepted;
    return store.install(prepared);
}
} // namespace streampack_implementation::orbital_material

template struct orbital::object_detail::History<streampack_implementation::orbital_material::Physical>;
template class orbital::ObjectStore<streampack_implementation::orbital_material::Physical>;
template struct orbital::RepresentationSelection<streampack_implementation::orbital_material::Control>;
template void orbital::retain(orbital::Representation<streampack_implementation::orbital_material::Control>*) noexcept;
template void orbital::release(orbital::Representation<streampack_implementation::orbital_material::Control>*) noexcept;
template void orbital::retain(orbital::Part<streampack_implementation::orbital_material::Control>*) noexcept;
template void orbital::release(orbital::Part<streampack_implementation::orbital_material::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<streampack_implementation::orbital_material::Control>*) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<streampack_implementation::orbital_material::Control>*) noexcept;
