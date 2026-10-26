#include "material.h"

#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>

#include <cassert>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace frame_owner_example {
namespace detail {
struct interpretation_state {
    std::pmr::memory_resource *memory;
    std::size_t uses = 1;
    const interpretation value;
    interpretation_state(std::pmr::memory_resource &resource, interpretation &&meaning) noexcept
        : memory(&resource), value(std::move(meaning)) {}
};

void retain(interpretation_state *state) noexcept {
    if (!state) return;
    assert(state->uses != std::numeric_limits<std::size_t>::max());
    ++state->uses;
}
void release(interpretation_state *state) noexcept {
    if (!state || --state->uses) return;
    auto *memory = state->memory;
    std::destroy_at(state);
    memory->deallocate(state, sizeof(*state), alignof(interpretation_state));
}
}  // namespace detail

Control::Control(detail::interpretation_state *state, std::size_t first, std::size_t count) noexcept
    : state_(state), first_(first), count_(count) {}
const interpretation &Control::meaning() const noexcept {
    assert(state_);
    return state_->value;
}
bool Control::valid(std::pmr::memory_resource &memory) const noexcept {
    return state_ && state_->memory == &memory && !meaning().pipeline.empty() &&
        first_ <= meaning().part_extents.size() && count_ <= meaning().part_extents.size() - first_;
}
std::pmr::memory_resource *Control::resource() const noexcept { return state_ ? state_->memory : nullptr; }
orbital::PartId Control::id(std::size_t index) const noexcept {
    assert(state_ && index < count_);
    // Part identities are local to this material's Object. The full ObjectId is
    // checked independently at logical publication, never inferred from a PartId.
    return {meaning().object.low, first_ + index};
}
orbital::Result<std::size_t> Control::extent(std::size_t index) const noexcept {
    if (!state_ || index >= count_) return std::unexpected(orbital::Error::absent);
    return meaning().part_extents[first_ + index];
}
void Control::retain() const noexcept { detail::retain(state_); }
void Control::release() const noexcept { detail::release(state_); }
Control Control::copy_selected(void *, std::size_t index) const noexcept {
    assert(state_ && index < count_);
    // The returned handle is the entire selected proof: its immutable dependency
    // gets an independent use. No throwing copy/allocation or payload use occurs.
    detail::retain(state_);
    return {state_, first_ + index, 1};
}
void Control::destroy_selected() const noexcept { detail::release(state_); }

interpretation_owner::interpretation_owner(detail::interpretation_state *state) noexcept : state_(state) {}
interpretation_owner::interpretation_owner(interpretation_owner &&other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}
interpretation_owner &interpretation_owner::operator=(interpretation_owner &&other) noexcept {
    if (this != &other) {
        detail::release(state_);
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}
interpretation_owner::~interpretation_owner() { detail::release(state_); }
orbital::Result<interpretation_owner> interpretation_owner::make(
    std::pmr::memory_resource &memory, interpretation value) noexcept {
    if (value.pipeline.empty()) return std::unexpected(orbital::Error::invalid_argument);
    try {
        auto *storage = static_cast<detail::interpretation_state *>(
            memory.allocate(sizeof(detail::interpretation_state), alignof(detail::interpretation_state)));
        return interpretation_owner{std::construct_at(storage, memory, std::move(value))};
    } catch (const std::bad_alloc &) { return std::unexpected(orbital::Error::capacity); }
}
const interpretation &interpretation_owner::meaning() const noexcept { return control().meaning(); }
Control interpretation_owner::control() const noexcept {
    return state_ ? Control{state_, 0, state_->value.part_extents.size()} : Control{};
}

void Physical::retain(Value *value) noexcept { orbital::retain(value); }
void Physical::release(Value *value) noexcept { orbital::release(value); }
orbital::Result<Physical::Value *> Physical::capture(std::pmr::memory_resource &memory,
    const Control &control, std::span<const PartSource> parts) noexcept {
    return orbital::capture_representation(memory, control,
        orbital::PartOwners::independent_mixed, parts);
}
bool Physical::accepts(Value *value, orbital::ObjectId object, std::uint64_t) noexcept {
    return value && Selection{value}.control().meaning().object == object;
}
bool Physical::accepts_resource(Value *value, std::pmr::memory_resource &memory) noexcept {
    return value && Selection{value}.control().valid(memory);
}
bool Physical::compatible(Value *left, Value *right) noexcept {
    if (!left || !right) return false;
    const auto &a = Selection{left}.control().meaning(), &b = Selection{right}.control().meaning();
    return a.object == b.object && a.logical_schema == b.logical_schema && a.rows == b.rows;
}
Physical::Footprint Physical::footprint(Value *value) noexcept {
    return {orbital::object_detail::representation_bytes(Selection{value}.control(),
        orbital::PartOwners::independent_mixed), 0};
}
orbital::Result<orbital::Part<Control> *> Physical::export_part(Selection selected,
    std::size_t index, orbital::ObjectBinding binding, ExportPolicy policy) noexcept {
    return orbital::object_detail::retain_part(selected, index, binding, policy);
}
bool same_material(Selection a, Selection b) noexcept {
    if (!a || !b || a.part_count() != b.part_count()) return false;
    const auto ac = a.control(), bc = b.control();
    if (ac.resource() != bc.resource() || ac.meaning() != bc.meaning()) return false;
    for (std::size_t i = 0; i < a.part_count(); ++i) {
        const auto ae = ac.extent(i), be = bc.extent(i);
        const auto av = a.part(i), bv = b.part(i);
        if (ac.id(i) != bc.id(i) || !ae || !be || *ae != *be || !av || !bv || av->address() != bv->address())
            return false;
    }
    return true;
}
orbital::Result<Store::Owner> capture(Store &store, const interpretation_owner &meaning,
    std::span<const orbital::PartSource> parts) {
    return store.capture(meaning.control(), parts);
}
orbital::Result<Store::Owner> capture(Store &store, std::pmr::memory_resource &memory,
    interpretation value, std::span<const orbital::PartSource> parts) {
    auto meaning = interpretation_owner::make(memory, std::move(value));
    if (!meaning) return std::unexpected(meaning.error());
    return capture(store, *meaning, parts);
}
orbital::Result<orbital::PartSource> reuse(Selection selected, std::size_t index) noexcept {
    return orbital::source(selected, index);
}
orbital::Result<orbital::PartSource> reuse(const orbital::Part<Control> *part) noexcept {
    return orbital::source(part);
}
}  // namespace frame_owner_example

template struct orbital::object_detail::History<frame_owner_example::Physical>;
template class orbital::ObjectStore<frame_owner_example::Physical>;
template struct orbital::RepresentationSelection<frame_owner_example::Control>;
template void orbital::release(orbital::Part<frame_owner_example::Control> *) noexcept;
template orbital::View orbital::view(const orbital::Part<frame_owner_example::Control> *) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<frame_owner_example::Control> *) noexcept;
