#include "group_fixture.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
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
void clone_boundaries() {
  const auto page=std::size_t(sysconf(_SC_PAGESIZE));
  check(page>=1088+64,"host page too small for guard test");
  auto* src=static_cast<std::byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  auto* dst=static_cast<std::byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  check(src!=MAP_FAILED && dst!=MAP_FAILED,"guard mapping failed");
  check(!mprotect(src+page,page,PROT_NONE) && !mprotect(dst+page,page,PROT_NONE),
      "guard protection failed");
  for (unsigned blocks=1;blocks<=17;++blocks) {
    const auto bytes=blocks*64;
    auto* source=src+page-bytes;
    auto* destination=dst+page-bytes;
    std::vector<std::byte> oracle(bytes),source_copy(bytes);
    for (unsigned i=0;i<bytes;++i) oracle[i]=std::byte((i*71+blocks*13)&255);
    source_copy=oracle;
    std::memcpy(source,oracle.data(),bytes);
    for (unsigned offset:{0u,bytes-8}) {
      std::memset(destination-64,0xa5,bytes+64);
      const auto value=UINT64_C(0x1f9acde087654321)+blocks+offset;
      std::memcpy(oracle.data()+offset,&value,8);
      idx_asm_clone_patch64(destination,source,blocks,offset,value);
      check(!std::memcmp(destination,oracle.data(),bytes),"clone/patch complete bytes differ");
      check(!std::memcmp(source,source_copy.data(),bytes),"clone overwrote sealed source");
      for (unsigned i=0;i<64;++i) check(destination[-64+int(i)]==std::byte{0xa5},
          "clone wrote before private destination");
      oracle=source_copy;
    }
  }
  check(!munmap(src,page*2) && !munmap(dst,page*2),"guard unmap failed");
}
Record record(std::uint32_t prefix) {
  Record r{prefix};
  for (unsigned i=0;i<8;++i) {
    const std::uint64_t metadata=UINT64_C(0x9e3779b97f4a7c15)*(prefix+1ULL+i*5);
    const std::uint64_t material=UINT64_C(0x100000001b3)*(prefix+7ULL+i*11);
    std::memcpy(r.descriptor.data()+i*8,&metadata,8);
    std::memcpy(r.material.data()+i*8,&material,8);
  }
  return r;
}
void expect(GroupFixture& fixture,const Store::Scope& scope,const Record& oracle) {
  ReadContext context{fixture.directory_namespace(),&scope,{}};
  const auto view=fixture.view(context);
  Lookup lookup;
  const bool graph=fixture.directory_namespace()==GroupFixture::graph_space;
  const auto* descriptor=graph?fixture.read(view,oracle.prefix):
      idx_asm_group16(&view,oracle.prefix,&lookup);
  check(descriptor,"existing original suffix missing");
  check(fixture.read(view,oracle.prefix)==descriptor,"read-only assembly changed selected bytes");
  if (graph) {
    const auto root=idx_module_bind(&view,view.root);
    check(idx_asm_graph32_bound(root.bytes,oracle.prefix)==descriptor,"qualified bound-root traversal differs");
  }
  check(!std::memcmp(descriptor+8,oracle.descriptor.data()+8,56),"metadata outside locator changed");
  const std::byte* pointer;
  std::memcpy(&pointer,descriptor,8);
  check(!std::memcmp(pointer,oracle.material.data(),64),"raw directory locator selected wrong useful bytes");
  const auto bound=scope.bind(GroupFixture::material_id(oracle.prefix));
  check(bool(bound),"coupled segment Object absent");
  check(take(bound.part(0)).address()==pointer,"directory locator and selected material disagree");
  if (!graph) {
    const auto leaf=scope.bind({GroupFixture::directory_space,lookup.leaf_name});
    check(bool(leaf) && lookup.payload_selection==leaf.realization && lookup.part==0,
        "group provenance not the actual physical selection");
  }
}
void run(loom::Worker& worker,bool graph,unsigned grain=16) {
  std::vector<Record> input;
  for (unsigned i=32;i<48;++i) input.push_back(record(i));
  input.push_back(record(128)); input.push_back(record(143));
  input.push_back(record(UINT32_MAX));
  GroupFixture fixture(worker,input,nullptr,graph,grain);
  Store::Scope old(fixture.store()),late(fixture.store());
  for (const auto& r:input) expect(fixture,old,r);
  ReadContext c{fixture.directory_namespace(),&old,{}};
  const auto view=fixture.view(c);
  for (unsigned miss:{0u,31u,48u,129u,142u,144u,UINT32_MAX-1})
    check(!fixture.read(view,miss),"hole/absent group was a hit");
  c.scope=nullptr; c.borrowed=fixture.store().borrow();
  check(fixture.read(view,UINT32_MAX),"max prefix Borrowed failed");
  // Stop using Borrowed before publication. Old/late Scopes remain stationary.
  check(bool(fixture.replace(128,3,UINT64_C(0x0123456789abcdef))),"real locator replacement refused");
  fixture.store().drain(); // late discovery must survive actual reclamation
  for (const auto& r:input) expect(fixture,late,r); // first discovery after install
  Store::Scope fresh(fixture.store());
  auto changed=input[16];
  const auto new_value=UINT64_C(0x0123456789abcdef);
  std::memcpy(changed.material.data()+24,&new_value,8);
  expect(fixture,fresh,changed);
  for (unsigned i=0;i<input.size();++i) if (i!=16) expect(fixture,fresh,input[i]);
  check(!fixture.replace(129,0,1),"absent suffix became an inserted segment");
  check(!fixture.replace(128,8,1),"out-of-bounds field admitted");
  // Physical admission must reject a finished material relabeled under another
  // full Object identity. Generic capture alone is not logical association.
  const auto material=fresh.bind(GroupFixture::material_id(128));
  const std::array changes{Store::Change{{GroupFixture::directory_space,129},
      Store::Owner::retain(material.realization)}};
  Store::Prepared refused(fixture.store());
  check(!fixture.store().prepare(fresh,changes,refused),"foreign namespace/name admitted");
}
struct FaultResource final:std::pmr::memory_resource {
  unsigned fail_at{},calls{},succeeded{};
  std::size_t live{};
  void* do_allocate(std::size_t bytes,std::size_t align) override {
    if (fail_at && ++calls==fail_at) throw std::bad_alloc{};
    auto* p=std::pmr::new_delete_resource()->allocate(bytes,align);
    live+=bytes;
    if (fail_at) ++succeeded;
    return p;
  }
  void do_deallocate(void* p,std::size_t bytes,std::size_t align) override {
    check(live>=bytes,"metadata accounting underflow"); live-=bytes;
    std::pmr::new_delete_resource()->deallocate(p,bytes,align);
  }
  bool do_is_equal(const std::pmr::memory_resource& r) const noexcept override { return this==&r; }
};
void partial_refusal(loom::Worker& worker,bool graph,unsigned fail_at) {
  FaultResource memory;
  const std::array input{record(17)};
  {
    GroupFixture fixture(worker,input,&memory,graph);
    Store::Scope late(fixture.store());
    const auto before=memory.live;
    // Native capture allocates one descriptor. Refuse the second descriptor:
    // the new data Owner has already consumed its real Allocation, while the
    // directory Allocation is still private. Neither may leak or publish.
    memory.fail_at=fail_at;
    const auto result=fixture.replace(17,0,123);
    memory.fail_at=0;
    check(!result && result.error()==orbital::Error::capacity,"late capture refusal not surfaced");
    check(memory.calls==fail_at && memory.succeeded==fail_at-1,"refusal did not follow acquired candidate metadata");
    check(memory.live==before,"partial candidate metadata not released");
    fixture.store().drain();
    expect(fixture,late,input[0]);
    Store::Scope fresh(fixture.store()); expect(fixture,fresh,input[0]);
    check(bool(fixture.replace(17,0,456)),"refusal left publication exclusion or backing stuck");
  }
  check(!memory.live,"fixture left metadata after partial refusal/shutdown");
}
void exported_directory_graph(loom::Worker& worker) {
  const std::array input{record(17)};
  GroupFixture fixture(worker,input,nullptr,true);
  orbital::Part<Control>* escaped;
  std::array<std::byte,64> expected_descriptor;
  {
    Store::Scope issuing(fixture.store());
    ReadContext context{fixture.directory_namespace(),&issuing,{}};
    const auto view=fixture.view(context);
    idx_asm_copy64(expected_descriptor.data(),fixture.read(view,17));
    auto bound=take(fixture.store().bound(issuing,{GroupFixture::graph_space,1}));
    escaped=take(bound.export_part(0,orbital::PartRetention::selected_copy));
  }
  for (unsigned i=0;i<64;++i) {
    check(bool(fixture.replace(17,0,1000+i)),"graph churn refused");
    fixture.store().drain();
  }
  // Escaped root Part owns its physical DIRECTORY graph independently. It does
  // not preserve separately versioned segment data after the issuing Scope.
  // Read opaque dirent bytes only; never dereference its old material pointer.
  auto* node=orbital::view(escaped).address();
  const unsigned group=17>>4;
  while (true) {
    const auto shift=std::to_integer<unsigned>(node[2]);
    const auto slot=(group>>shift)&31;
    const std::byte* child;
    std::memcpy(&child,node+64+slot*16,8);
    check(child,"escaped directory child lost");
    if (shift) { node=child; continue; }
    std::uint16_t mask; std::memcpy(&mask,node+64+slot*16+8,2);
    unsigned rank=0;
    for (unsigned suffix=0;suffix<(17&15);++suffix) rank+=(mask>>suffix)&1;
    check(!std::memcmp(child+rank*64,expected_descriptor.data(),64),
        "escaped root failed to preserve child physical bytes/interpretation");
    break;
  }
  orbital::release(escaped);
}
void late_distinct_cuts(loom::Worker& worker,bool graph,unsigned grain=16) {
  std::array<Record,64> input;
  for (unsigned i=0;i<input.size();++i) input[i]=record(i*16);
  GroupFixture fixture(worker,input,nullptr,graph,grain);
  std::array<std::optional<Store::Scope>,65> readers;
  readers[0].emplace(fixture.store());
  for (unsigned i=0;i<input.size();++i) {
    check(bool(fixture.replace(input[i].prefix,0,1000+i)),"distinct-cut change refused");
    readers[i+1].emplace(fixture.store());
  }
  fixture.store().drain();
  for (unsigned cut=0;cut<readers.size();++cut) for (unsigned row=0;row<input.size();++row) {
    auto oracle=input[row];
    if (row<cut) {
      const std::uint64_t value=1000+row;
      std::memcpy(oracle.material.data(),&value,8);
    }
    expect(fixture,*readers[cut],oracle);
  }
}
void directory_arrays(loom::Worker& worker,unsigned grain) {
  std::array<Record,257> input;
  for (unsigned i=0;i<input.size();++i) input[i]=record(i);
  const auto before=orbital::allocation::statistics(*worker.memory);
  GroupFixture fixture(worker,input,nullptr,true,grain);
  const auto after=orbital::allocation::statistics(*worker.memory);
  const auto arrays=(input.size()+grain-1)/grain;
  check(fixture.initial_directory_allocations()==arrays,"physical directory allocation census");
  check(fixture.initial_directory_bytes()==input.size()*64,"header-free directory byte census");
  check(after.allocations-before.allocations==input.size()+arrays+fixture.initial_graph_nodes(),
      "Segment/directory/node allocation cardinalities differ");
  Store::Scope old(fixture.store());
  for (unsigned i:{0u,15u,16u,63u,64u,255u,256u}) {
    expect(fixture,old,input[i]);
    check(bool(fixture.replace(i,7,UINT64_MAX-i)),"array boundary replacement refused");
  }
  fixture.store().drain();
  for (const auto& r:input) expect(fixture,old,r);
  Store::Scope fresh(fixture.store());
  for (unsigned i=0;i<input.size();++i) {
    auto wanted=input[i];
    for (unsigned changed:{0u,15u,16u,63u,64u,255u,256u}) if (i==changed) {
      const auto v=UINT64_MAX-i; std::memcpy(wanted.material.data()+56,&v,8);
    }
    expect(fixture,fresh,wanted);
  }
}
}
int main() {
  clone_boundaries();
  loom::Worker worker;
  check(bool(loom::adopt(worker)),"Worker adoption failed");
  auto arena=take(orbital::allocation::open_arena(16u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configuration failed");
  run(worker,false); run(worker,true);
  for (unsigned grain:{16u,64u,256u}) {
    directory_arrays(worker,grain);
    if (grain!=16) { run(worker,true,grain); late_distinct_cuts(worker,true,grain); }
  }
  partial_refusal(worker,false,2);
  partial_refusal(worker,true,3); // after both data/group Owners, before proof
  partial_refusal(worker,true,4); // after retained children/proof, root capture
  exported_directory_graph(worker);
  late_distinct_cuts(worker,false); late_distinct_cuts(worker,true);
  while (true) {
    const auto progress=loom::shutdown(worker);
    check(progress.kind()!=orbital::Progress::Kind::failed,"physical shutdown failed");
    if (progress.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::puts("PASS: direct group full/holes/max, real Part locator coupled publication, late old/fresh readers, exact64 material, clone guards; no timing claim");
}
