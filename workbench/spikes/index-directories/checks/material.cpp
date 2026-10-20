#include "group_fixture.h"
#include <sys/mman.h>
#include <unistd.h>
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
  ReadContext context{GroupFixture::directory_space,&scope,{}};
  const auto view=fixture.view(context);
  Lookup lookup;
  const auto* descriptor=idx_asm_group16(&view,oracle.prefix,&lookup);
  check(descriptor,"existing original suffix missing");
  check(idx_asm_group16(&view,oracle.prefix,nullptr)==descriptor,"optional mutation provenance differs");
  check(!std::memcmp(descriptor+8,oracle.descriptor.data()+8,56),"metadata outside locator changed");
  const std::byte* pointer;
  std::memcpy(&pointer,descriptor,8);
  check(!std::memcmp(pointer,oracle.material.data(),64),"raw directory locator selected wrong useful bytes");
  const auto bound=scope.bind(GroupFixture::material_id(oracle.prefix));
  check(bool(bound),"coupled segment Object absent");
  check(take(bound.part(0)).address()==pointer,"directory locator and selected material disagree");
  const auto leaf=scope.bind({GroupFixture::directory_space,lookup.leaf_name});
  check(bool(leaf) && lookup.payload_selection==leaf.realization && lookup.part==0,
      "group provenance not the actual physical selection");
}
void run(loom::Worker& worker) {
  std::vector<Record> input;
  for (unsigned i=32;i<48;++i) input.push_back(record(i));
  input.push_back(record(128)); input.push_back(record(143));
  input.push_back(record(UINT32_MAX));
  GroupFixture fixture(worker,input);
  Store::Scope old(fixture.store()),late(fixture.store());
  for (const auto& r:input) expect(fixture,old,r);
  ReadContext c{GroupFixture::directory_space,&old,{}};
  const auto view=fixture.view(c);
  for (unsigned miss:{0u,31u,48u,129u,142u,144u,UINT32_MAX-1})
    check(!idx_asm_group16(&view,miss,nullptr),"hole/absent group was a hit");
  c.scope=nullptr; c.borrowed=fixture.store().borrow();
  check(idx_asm_group16(&view,UINT32_MAX,nullptr),"max prefix Borrowed failed");
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
void partial_refusal(loom::Worker& worker) {
  FaultResource memory;
  const std::array input{record(17)};
  {
    GroupFixture fixture(worker,input,&memory);
    Store::Scope late(fixture.store());
    const auto before=memory.live;
    // Native capture allocates one descriptor. Refuse the second descriptor:
    // the new data Owner has already consumed its real Allocation, while the
    // directory Allocation is still private. Neither may leak or publish.
    memory.fail_at=2;
    const auto result=fixture.replace(17,0,123);
    memory.fail_at=0;
    check(!result && result.error()==orbital::Error::capacity,"late capture refusal not surfaced");
    check(memory.calls==2 && memory.succeeded==1,"refusal did not follow acquired candidate metadata");
    check(memory.live==before,"partial candidate metadata not released");
    fixture.store().drain();
    expect(fixture,late,input[0]);
    Store::Scope fresh(fixture.store()); expect(fixture,fresh,input[0]);
    check(bool(fixture.replace(17,0,456)),"refusal left publication exclusion or backing stuck");
  }
  check(!memory.live,"fixture left metadata after partial refusal/shutdown");
}
}
int main() {
  clone_boundaries();
  loom::Worker worker;
  check(bool(loom::adopt(worker)),"Worker adoption failed");
  auto arena=take(orbital::allocation::open_arena(16u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configuration failed");
  run(worker); partial_refusal(worker);
  while (true) {
    const auto progress=loom::shutdown(worker);
    check(progress.kind()!=orbital::Progress::Kind::failed,"physical shutdown failed");
    if (progress.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::puts("PASS: direct group full/holes/max, real Part locator coupled publication, late old/fresh readers, exact64 material, clone guards; no timing claim");
}
