#include "object_material.h"
#include <object/store_impl.h>
#include <cassert>

namespace object_test {
std::uint64_t Selection::number() const noexcept {
  assert(value);
  return value->specification.complemented ? ~value->encoded : value->encoded;
}
void Physical::retain(Value* value) noexcept { if (value) ++value->references; }
void Physical::release(Value* value) noexcept {
  if (!value || --value->references) return;
  auto* memory = value->memory;
  const auto specification = value->specification;
  std::destroy_at(value);
  memory->deallocate(value, sizeof(Value), alignof(Value));
  // Physical disposal, unlike a memory_resource callback, may reenter the catalog.
  if (specification.disposed) specification.disposed(specification.context);
}
orbital::Result<Value*> Physical::capture(std::pmr::memory_resource& memory,
    const Specification& specification, std::span<const PartSource> parts) noexcept {
  if (!parts.empty()) return std::unexpected(orbital::Error::invalid_argument);
  try {
    return std::construct_at(static_cast<Value*>(memory.allocate(sizeof(Value), alignof(Value))),
        Value{&memory, 1, specification, specification.complemented ? ~specification.number : specification.number});
  } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
Export::Export(Value* owner, orbital::ObjectBinding association) noexcept : value(owner), binding(association) {
  Physical::retain(value);
}
Export::Export(Export&& other) noexcept : value(std::exchange(other.value, nullptr)), binding(other.binding) {}
Export::~Export() { Physical::release(value); }
orbital::Result<Export> Physical::export_part(Selection selected, std::size_t index,
    orbital::ObjectBinding binding, ExportPolicy) noexcept {
  if (!selected || index) return std::unexpected(orbital::Error::absent);
  return Export{selected.value, binding};
}
} // namespace object_test

// Cold publication/conversion/maintenance compile here, separately from callers.
template struct orbital::object_detail::History<object_test::Physical>;
template class orbital::ObjectStore<object_test::Physical>;
