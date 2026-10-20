#include "material.h"
#include <loom/worker.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <vector>

namespace {
using namespace index_spike;
void check(bool yes,const char* why,
    std::source_location at=std::source_location::current()) {
  if (yes) return;
  std::fprintf(stderr,"%s:%u: %s\n",at.file_name(),at.line(),why); std::abort();
}
template<class T> T take(orbital::Result<T> r) {
  check(bool(r),"actual module refused fixture work"); return std::move(*r);
}
constexpr std::uint64_t space=0x49445841534d;
using Bytes=std::vector<std::byte>;
template<class T> void put(Bytes& b,std::size_t at,T value) {
  std::memcpy(b.data()+at,&value,sizeof(value));
}
std::array<std::byte,64> expected(unsigned leaf,unsigned rank) {
  std::array<std::byte,64> out;
  for (unsigned i=0;i<8;++i) {
    const std::uint64_t word=(0x9e3779b97f4a7c15ULL*(rank+1+i*11+leaf*123))
        ^ (0x100000001b3ULL<<(i&7));
    std::memcpy(out.data()+i*8,&word,8);
  }
  return out;
}
std::uint32_t key(unsigned leaf,unsigned rank) {
  return leaf==1 && rank==31 ? UINT32_MAX : leaf*10000+rank*17+13;
}
Store::Owner capture(Store& store,std::pmr::memory_resource& memory,
    loom::Worker& worker,std::uint64_t id,const Format& format,
    const std::vector<Bytes>& bodies) {
  check(format.count==bodies.size(),"fixture format/source mismatch");
  std::size_t total=0; for (const auto& b:bodies) total+=b.size();
  auto backing=take(loom::allocate(worker,{.bytes=total,.alignment=64}));
  check(format.memory==&memory,"capture format resource mismatch");
  const Control c{&format,id,0,format.count,
      orbital::PartOwners::grouped_allocation,ExtentMode::parts};
  std::vector<orbital::PartSource> sources;
  std::size_t offset=0;
  for (std::size_t i=0;i<bodies.size();++i) {
    check(bodies[i].size()==format.bytes[i],"wrong body extent");
    std::memcpy(backing.address+offset,bodies[i].data(),bodies[i].size());
    sources.push_back({c.id(i),loom::bytes(backing).subspan(offset,bodies[i].size()),{},&backing});
    offset+=bodies[i].size();
  }
  return take(store.capture(c,sources));
}
void publish(Store& s,const std::vector<Store::Change>& changes) {
  Store::Scope basis(s); Store::Prepared packet(s);
  check(bool(s.prepare(basis,changes,packet)),"prepare refused");
  check(bool(s.install(packet)),"install refused");
}
void run(loom::Worker& worker,bool colocated) {
  std::pmr::unsynchronized_pool_resource memory;
  const Format root_format{{704},1,0,nullptr,&memory};
  const Format packed_format{{512,512,512,512,512},5,0,nullptr,&memory};
  const Format single_format{{512},1,0,nullptr,&memory};
  const Format meta_format{{576},1,0,nullptr,&memory};
  Store store(memory);
  std::pmr::unsynchronized_pool_resource wrong_memory;
  auto wrong_format=single_format; wrong_format.memory=&wrong_memory;
  auto rejected_backing=take(loom::allocate(worker,{.bytes=512,.alignment=64}));
  std::memset(rejected_backing.address,0,512);
  const Control wrong_resource{&wrong_format,99,0,1};
  const std::array wrong_sources{orbital::PartSource{wrong_resource.id(0),
      loom::bytes(rejected_backing),{},&rejected_backing}};
  check(!store.capture(wrong_resource,wrong_sources),"format resource mismatch admitted actual valid Part");
  check(bool(rejected_backing),"resource refusal consumed fresh backing");
  Control overflow{&single_format,99,UINT32_MAX,1};
  check(!overflow.valid(memory),"wrapping Control admitted invalid geometry");
  check(!store.capture(overflow,{}),"wrapping Control reached geometry reads");
  Format too_many=single_format; too_many.count=18;
  Control beyond_array{&too_many,99,0,1};
  check(!beyond_array.valid(memory),"oversized Format admitted invalid geometry");
  check(!store.capture(beyond_array,{}),"oversized Format reached geometry reads");
  std::vector<Store::Change> initial;
  Bytes root(704);
  put(root,0,std::uint16_t{2}); put(root,2,std::uint8_t{1});
  put(root,64,key(0,0)); put(root,68,key(1,0));
  put(root,448,std::uint64_t{2}); put(root,456,std::uint64_t{3});
  initial.push_back({{space,1},capture(store,memory,worker,1,root_format,{root})});
  for (unsigned leaf=0;leaf<2;++leaf) {
    // Independent names use48..79; prefixes start128. Co-located prefixes64.
    const unsigned prefix=colocated?64:128;
    const unsigned slots=colocated?448:512;
    Bytes meta(colocated?512:576);
    put(meta,0,std::uint16_t{32});
    std::vector<Bytes> data(4,Bytes(512));
    for (unsigned rank=0;rank<32;++rank) {
      put(meta,prefix+rank*4,key(leaf,rank));
      const unsigned slot=(rank*7)&31;
      meta[slots+rank]=std::byte(slot);
      const auto value=expected(leaf,rank);
      std::memcpy(data[slot/8].data()+(slot%8)*64,value.data(),64);
    }
    if (colocated) {
      std::vector<Bytes> bodies; bodies.push_back(std::move(meta));
      for (auto& d:data) bodies.push_back(std::move(d));
      initial.push_back({{space,2+leaf},capture(store,memory,worker,2+leaf,packed_format,bodies)});
    } else {
      for (unsigned c=0;c<4;++c) {
        const auto id=100+leaf*4+c;
        put(meta,48+c*8,std::uint64_t(id));
        initial.push_back({{space,id},capture(store,memory,worker,id,single_format,{data[c]})});
      }
      initial.push_back({{space,2+leaf},capture(store,memory,worker,2+leaf,meta_format,{meta})});
    }
  }
  publish(store,initial); initial.clear();
  Store::Scope old(store),late(store);
  ReadContext context{space,&old,{}};
  const NativeRead read{&context,1,64,448,colocated?64u:128u,
      colocated?448u:512u,48,3,colocated?1u:0u,0};
  for (unsigned leaf=0;leaf<2;++leaf) for (unsigned rank=0;rank<32;++rank) {
    Lookup found;
    const auto* bytes=idx_asm_exact(&read,key(leaf,rank),&found);
    const auto oracle=expected(leaf,rank);
    check(bytes && !std::memcmp(bytes,oracle.data(),64),"assembly selected wrong value");
    check(found.rank==rank && found.slot==((rank*7)&31),"lost ordered-rank/slot distinction");
    std::uint64_t digest=0;
    for (unsigned i=0;i<8;++i) { std::uint64_t word; std::memcpy(&word,oracle.data()+i*8,8); digest^=word; }
    check(idx_asm_digest64(bytes)==digest,"ISA full64 consumer differs");
    std::array<std::byte,64> copy;
    idx_asm_copy64(copy.data(),bytes);
    check(copy==oracle,"ISA private64 copy differs");
    check(idx_asm_exact(&read,key(leaf,rank),nullptr)==bytes,"optional provenance changed lookup");
  }
  for (auto miss:{0u,14u,9999u,10014u,UINT32_MAX-1})
    check(!idx_asm_exact(&read,miss,nullptr),"exact-prefix gap was treated as a hit");
  context.scope=nullptr; context.borrowed=store.borrow();
  check(idx_asm_exact(&read,key(0,0),nullptr)!=nullptr,"Borrowed ready call failed");
  // Native material access and data/publication changes are the next vertical
  // treatment; this first check claims exact metadata selection, not row/e2e work.
}
}
int main() {
  loom::Worker worker;
  check(bool(loom::adopt(worker)),"Worker adoption failed");
  auto arena=take(orbital::allocation::open_arena(16u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configure failed");
  run(worker,true); run(worker,false);
  while (true) {
    auto p=loom::shutdown(worker);
    check(p.kind()!=orbital::Progress::Kind::failed,"physical shutdown failed");
    if (p.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::puts("PASS: real Orbital/Ikea/Loom selected directory assembly, co-located/independent, hit/gap/max-prefix, slot permutation,64-byte ISA consumer; no timing claim");
}
