#include "material.h"

extern "C" index_spike::NodeBorrow idx_module_bind(
    const index_spike::NativeRead* view,std::uint64_t name) noexcept {
  const auto& c=*static_cast<const index_spike::ReadContext*>(view->context);
  const orbital::ObjectId id{c.directory_namespace,name};
  const auto selected=c.scope ? c.scope->bind(id) : c.borrowed.bind(id);
  if (!selected) return {};
  const auto part=selected.part(std::size_t{0});
  if (!part) return {};
  return {part->address(),selected.realization};
}
extern "C" const std::byte* idx_module_part(void* value,std::uint32_t ordinal) noexcept {
  const index_spike::Selection selected{
      static_cast<index_spike::Physical::Value*>(value)};
  if (!selected) return nullptr;
  const auto part=selected.part(std::size_t(ordinal));
  return part ? part->address() : nullptr;
}
