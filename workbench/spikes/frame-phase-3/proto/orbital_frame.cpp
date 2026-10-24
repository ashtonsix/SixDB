// The frame material's cold Orbital operations, compiled once (orbital/docs/material.md).
#include "orbital_frame.h"

#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>

namespace frame3_orbital {
void Physical::retain(Value *value) noexcept { orbital::retain(value); }
void Physical::release(Value *value) noexcept { orbital::release(value); }
orbital::Result<Physical::Value *> Physical::capture(std::pmr::memory_resource &memory, const Control &control, std::span<const PartSource> parts) noexcept {
    return orbital::capture_representation(memory, control, orbital::PartOwners::independent_shared, parts);
}
bool Physical::accepts_resource(Value *value, std::pmr::memory_resource &memory) noexcept {
    return value && Selection{value}.control().resource() == &memory;
}
bool Physical::compatible(Value *left, Value *right) noexcept {
    return Selection{left}.control().geometry->object == Selection{right}.control().geometry->object;
}
Physical::Footprint Physical::footprint(Value *value) noexcept {
    return {orbital::object_detail::representation_bytes(Selection{value}.control(), orbital::PartOwners::independent_shared), 0};
}
orbital::Result<orbital::Part<Control> *> Physical::export_part(Selection selected, std::size_t index, orbital::ObjectBinding binding,
                                                                 ExportPolicy policy) noexcept {
    return orbital::object_detail::retain_part(selected, index, binding, policy);
}
orbital::Result<orbital::PartSource> reuse(Selection selected, std::size_t index) noexcept { return orbital::source(selected, index); }
}  // namespace frame3_orbital

template struct orbital::object_detail::History<frame3_orbital::Physical>;
template class orbital::ObjectStore<frame3_orbital::Physical>;
template struct orbital::RepresentationSelection<frame3_orbital::Control>;
