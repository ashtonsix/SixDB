#include <engine/directory.h>
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>

// Orbital material for directory nodes. Part extents follow from the node's
// row count and the layer's key width, so the Control carries no pointers
// beyond its resource. Parts: header (count, height), prefixes or separators,
// [min/max fences | logical separators], dirent tiles or child names.
namespace engine::directory {
namespace {
std::size_t rounded(std::size_t n) {return (n+63)/64*64;}
}

std::size_t Control::full_part_count() const noexcept {
  if(height) return 3+(fenced?1:0);
  return 2+(fenced?2:0)+(count+tile-1)/tile;
}

std::size_t Control::full_extent(std::size_t i) const noexcept {
  const std::size_t n=count,k=key_bytes;
  if(i==0) return 8;
  if(i==1) return 4*n;
  if(height) return fenced&&i==2?k*n:8*n;
  if(fenced&&(i==2||i==3)) return k*n;
  auto t=i-(fenced?4:2);
  return 64*std::min<std::size_t>(tile,n-t*tile);
}

std::size_t Control::part_count() const noexcept {return projected?1:full_part_count();}

orbital::Result<std::size_t> Control::extent(std::size_t i) const noexcept {
  if(i>=part_count()) return std::unexpected(orbital::Error::invalid_argument);
  return full_extent(first+i);
}

std::uint32_t Control::padding(std::size_t i) const noexcept {
  auto e=full_extent(first+i);
  return std::uint32_t(rounded(e)-e);
}

bool Control::valid(std::pmr::memory_resource& r) const noexcept {
  return memory==&r&&count&&tile&&(!fenced||key_bytes)&&!reserved&&projected<=1&&
         (projected?first<full_part_count():first==0);
}

void Physical::retain(Value* p) noexcept {orbital::retain(p);}
void Physical::release(Value* p) noexcept {orbital::release(p);}
orbital::Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& r,
    const Control& c,std::span<const PartSource> p) noexcept {
  return orbital::capture_representation(r,c,orbital::PartOwners::independent_allocation,p);
}
bool Physical::accepts(Value* p,orbital::ObjectId id,std::uint64_t) noexcept {
  if(!p) return false;
  auto c=Selection{p}.control();
  return !c.projected&&c.name==id.low;
}
bool Physical::accepts_resource(Value* p,std::pmr::memory_resource& r) noexcept {
  return p&&Selection{p}.control().resource()==&r;
}
bool Physical::compatible(Value* a,Value* b) noexcept {
  auto x=Selection{a}.control(),y=Selection{b}.control();
  return x.name==y.name&&x.count==y.count&&x.height==y.height&&x.key_bytes==y.key_bytes&&
         x.fenced==y.fenced&&x.tile==y.tile&&x.first==y.first&&x.projected==y.projected;
}
Physical::Footprint Physical::footprint(Value* p) noexcept {
  return {orbital::object_detail::representation_bytes(Selection{p}.control(),
      orbital::PartOwners::independent_allocation),0};
}
orbital::Result<orbital::Part<Control>*> Physical::export_part(Selection s,std::size_t i,
    orbital::ObjectBinding b,ExportPolicy p) noexcept {
  return orbital::object_detail::retain_part(s,i,b,p);
}
}

template struct orbital::object_detail::History<engine::directory::Physical>;
template class orbital::ObjectStore<engine::directory::Physical>;
template struct orbital::RepresentationSelection<engine::directory::Control>;
template orbital::Result<orbital::PartSource> orbital::source(
    engine::directory::Selection,std::size_t) noexcept;
template void orbital::release(orbital::Part<engine::directory::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<engine::directory::Control>*) noexcept;
template orbital::ObjectBinding orbital::binding(const orbital::Part<engine::directory::Control>*) noexcept;
