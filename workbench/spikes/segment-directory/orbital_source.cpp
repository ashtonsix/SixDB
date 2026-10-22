#include "orbital_source.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace segdir {
namespace {
template<class T> T take(orbital::Result<T> r,const char* what) {
  if(!r) {std::fprintf(stderr,"orbital source: %s refused\n",what);std::abort();}
  return std::move(*r);
}
void take(orbital::Result<void> r,const char* what) {
  if(!r) {std::fprintf(stderr,"orbital source: %s refused\n",what);std::abort();}
}
std::size_t rounded(std::size_t n) {return (n+63)/64*64;}
}

OrbitalSource::OrbitalSource(loom::Worker& worker,std::pmr::memory_resource& memory,const Store& model,std::uint64_t space)
  :space_(space),store_(memory) {
  using engine::material::Store;
  std::vector<Store::Change> changes;
  auto publish=[&] {
    Store::Scope basis(store_);
    Store::Prepared packet(store_);
    take(store_.prepare(basis,changes,packet),"prepare");
    take(store_.install(packet),"install");
    changes.clear();
  };
  model.each([&](std::uint64_t name,const Object& o) {
    // One Definition per distinct geometry; one grouped allocation per Object.
    std::vector<std::uint64_t> sizes;
    std::size_t total=0;
    for(auto& p:o.parts) {sizes.push_back(p->bytes.size());total+=rounded(p->bytes.size());}
    auto it=definitions_.find(sizes);
    if(it==definitions_.end()) {
      std::vector<engine::material::Geometry> geometry;
      for(auto n:sizes) geometry.push_back({n,64,std::uint32_t(rounded(n)-n)});
      auto d=take(engine::material::Definition::native(memory,space_,std::uint32_t(definitions_.size()+1),geometry),"definition");
      it=definitions_.emplace(sizes,std::move(d)).first;
    }
    auto allocation=take(loom::allocate(worker,{.bytes=std::max<std::size_t>(64,total),.alignment=64}),"allocate");
    auto control=it->second.control(name,orbital::PartOwners::grouped_allocation);
    std::vector<orbital::PartSource> sources;
    std::size_t at=0;
    for(std::size_t i=0;i<o.parts.size();++i) {
      auto& b=o.parts[i]->bytes;
      std::memcpy(allocation.address+at,b.data(),b.size());
      std::memset(allocation.address+at+b.size(),0,rounded(b.size())-b.size());
      sources.push_back({control.id(i),{allocation.address+at,b.size()},{},&allocation});
      at+=rounded(b.size());
    }
    changes.push_back({{space_,name},take(store_.capture(control,sources),"capture")});
    if(changes.size()==4096) publish();
  });
  if(!changes.empty()) publish();
  scope_.emplace(store_);
}

OrbitalSource::~OrbitalSource() {scope_.reset();}
}
