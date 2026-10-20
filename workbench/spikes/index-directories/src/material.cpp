#include "material.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>

namespace index_spike {
DirectoryDependencies* DirectoryDependencies::make(std::pmr::memory_resource& memory,
    std::uint64_t ns,std::uint64_t name) {
  auto* raw=memory.allocate(sizeof(DirectoryDependencies),alignof(DirectoryDependencies));
  return std::construct_at(static_cast<DirectoryDependencies*>(raw),memory,ns,name);
}
void DirectoryDependencies::release() noexcept {
  if (--references) return;
  auto* resource=memory;
  std::destroy_at(this); // Can cascade through the actual child topology.
  resource->deallocate(this,sizeof(DirectoryDependencies),alignof(DirectoryDependencies));
}
void Control::retain() const noexcept {
  if (auto* proof=format->dependencies) ++proof->references;
}
void Control::release() const noexcept {
  if (auto* proof=format->dependencies) proof->release();
}
void Physical::retain(Value* v) noexcept { orbital::retain(v); }
void Physical::release(Value* v) noexcept { orbital::release(v); }
orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& r,
    const Control& c,std::span<const PartSource> p) noexcept {
  return orbital::capture_representation(r,c,c.owners,p);
}
bool Physical::accepts_resource(Value* v,std::pmr::memory_resource& r) noexcept {
  return v && Selection{v}.control().resource()==&r;
}
bool Physical::accepts(Value* v,orbital::ObjectId id,std::uint64_t) noexcept {
  if (!v) return false;
  const auto c=Selection{v}.control();
  return c.name==id.low && (!c.format->object_namespace ||
      c.format->object_namespace==id.high);
}
bool Physical::compatible(Value* a,Value* b) noexcept {
  auto x=Selection{a}.control(),y=Selection{b}.control();
  return x.format==y.format && x.first==y.first && x.count==y.count
      && x.extent_mode==y.extent_mode;
}
Physical::Footprint Physical::footprint(Value* v) noexcept {
  const auto c=Selection{v}.control();
  return {orbital::object_detail::representation_bytes(c,c.owners),0};
}
orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection s,
    std::size_t i,orbital::ObjectBinding b,ExportPolicy p) noexcept {
  return orbital::object_detail::retain_part(s,i,b,p);
}
}
template struct orbital::object_detail::History<index_spike::Physical>;
template class orbital::ObjectStore<index_spike::Physical>;
template struct orbital::RepresentationSelection<index_spike::Control>;
template orbital::Result<orbital::PartSource> orbital::source(
    index_spike::Selection,std::size_t) noexcept;
template orbital::View orbital::view(const orbital::Part<index_spike::Control>*) noexcept;
template void orbital::release(orbital::Part<index_spike::Control>*) noexcept;
