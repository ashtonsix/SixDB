#include "extension_material.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>

namespace extension_test {
void Physical::retain(Value* value) noexcept { orbital::retain(value); }

void Physical::release(Value* value) noexcept { orbital::release(value); }

orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& memory,
    const Control& control, std::span<const PartSource> parts) noexcept {
  return orbital::capture_representation(memory, control,
      orbital::PartOwners::independent_shared, parts);
}

bool Physical::accepts_resource(Value* value, std::pmr::memory_resource& memory) noexcept {
  return value && Selection{value}.control().resource() == &memory;
}

bool Physical::compatible(Value* left, Value* right) noexcept {
  const auto a = Selection{left}.control(), b = Selection{right}.control();
  return a.count == b.count && a.first == b.first;
}

Physical::Footprint Physical::footprint(Value* value) noexcept {
  const auto control = Selection{value}.control();
  return {orbital::object_detail::representation_bytes(control,
      orbital::PartOwners::independent_shared), 0};
}

orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection selected,
    std::size_t index, orbital::ObjectBinding binding, ExportPolicy policy) noexcept {
  return orbital::object_detail::retain_part(selected, index, binding, policy);
}
} // namespace extension_test

template struct orbital::object_detail::History<extension_test::Physical>;
template class orbital::ObjectStore<extension_test::Physical>;
template struct orbital::RepresentationSelection<extension_test::Control>;
template void orbital::release(orbital::Part<extension_test::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<extension_test::Control>*) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<extension_test::Control>*) noexcept;
