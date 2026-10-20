#include "material.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>

namespace index_spike {
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
  return x.format==y.format && x.first==y.first && x.count==y.count;
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
