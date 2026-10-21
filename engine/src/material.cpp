#include "material_internal.h"
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
#include <bit>
#include <limits>

namespace engine::material {
Layout::Layout(std::pmr::memory_resource& r,std::uint64_t ns,std::uint32_t rep,
    detail::Kind tag,std::span<const Geometry> geometry,std::shared_ptr<const void> proof)
    :memory(&r),object_namespace(ns),representation(rep),kind(tag),
      parts(geometry.begin(),geometry.end(),&r),interpretation(std::move(proof)) {}
void Layout::release() const noexcept {
  if (--references_) return;
  auto* r=memory;
  std::destroy_at(const_cast<Layout*>(this));
  r->deallocate(const_cast<Layout*>(this),sizeof(Layout),alignof(Layout));
}
orbital::Result<Definition> detail::Access::make(std::pmr::memory_resource& r,
    std::uint64_t ns,std::uint32_t rep,Kind kind,std::span<const Geometry> geometry,
    std::shared_ptr<const void> proof) {
  using orbital::Error;
  if (geometry.size()>UINT32_MAX) return std::unexpected(Error::capacity);
  for (auto p:geometry) if (!std::has_single_bit(p.alignment) ||
      p.bytes>std::numeric_limits<std::size_t>::max()-p.padding)
    return std::unexpected(Error::invalid_argument);
  void* memory=nullptr;
  try {
    memory=r.allocate(sizeof(Layout),alignof(Layout));
    return Definition(new(memory) Layout(r,ns,rep,kind,geometry,std::move(proof)));
  } catch (const std::bad_alloc&) {
    if (memory) r.deallocate(memory,sizeof(Layout),alignof(Layout));
    return std::unexpected(Error::capacity);
  }
}
orbital::Result<Definition> Definition::native(std::pmr::memory_resource& r,
    std::uint64_t ns,std::uint32_t rep,std::span<const Geometry> geometry,
    std::shared_ptr<const void> proof) {
  return detail::Access::make(r,ns,rep,detail::Kind::segment,geometry,std::move(proof));
}
Control Definition::control(std::uint64_t name,orbital::PartOwners owners) const noexcept {
  return {layout_,name,0,0,owners,0};
}
bool Control::valid(std::pmr::memory_resource& r) const noexcept {
  return layout && layout->memory==&r && !reserved && projected<=1 &&
      (projected?first<layout->parts.size():first==0);
}
orbital::Result<std::size_t> Control::extent(std::size_t i) const noexcept {
  if (i>=part_count()) return std::unexpected(orbital::Error::invalid_argument);
  return layout->parts[first+i].bytes;
}
void Physical::retain(Value* p) noexcept { orbital::retain(p); }
void Physical::release(Value* p) noexcept { orbital::release(p); }
orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& r,
    const Control& c,std::span<const PartSource> p) noexcept {
  return orbital::capture_representation(r,c,c.owners,p);
}
bool Physical::accepts(Value* p,orbital::ObjectId id,std::uint64_t) noexcept {
  if (!p) return false;
  const auto c=Selection{p}.control();
  return !c.projected && c.layout->object_namespace==id.high && c.name==id.low;
}
bool Physical::accepts_resource(Value* p,std::pmr::memory_resource& r) noexcept {
  return p && Selection{p}.control().resource()==&r;
}
bool Physical::compatible(Value* a,Value* b) noexcept {
  const auto x=Selection{a}.control(),y=Selection{b}.control();
  return x.layout==y.layout && x.name==y.name && x.first==y.first && x.projected==y.projected;
}
Physical::Footprint Physical::footprint(Value* p) noexcept {
  const auto c=Selection{p}.control();
  return {orbital::object_detail::representation_bytes(c,c.owners),0};
}
orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection s,std::size_t i,
    orbital::ObjectBinding b,ExportPolicy p) noexcept {
  return orbital::object_detail::retain_part(s,i,b,p);
}
}
template struct orbital::object_detail::History<engine::material::Physical>;
template class orbital::ObjectStore<engine::material::Physical>;
template struct orbital::RepresentationSelection<engine::material::Control>;
template orbital::Result<orbital::PartSource> orbital::source(
    engine::material::Selection,std::size_t) noexcept;
template orbital::Result<orbital::PartSource> orbital::source(
    const orbital::Part<engine::material::Control>*) noexcept;
template void orbital::retain(orbital::Part<engine::material::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<engine::material::Control>*) noexcept;
template void orbital::release(orbital::Part<engine::material::Control>*) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<engine::material::Control>*) noexcept;
