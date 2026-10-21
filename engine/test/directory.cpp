#include <engine/directory.h>
#include <loom/worker.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace {
using namespace engine;
using Store=material::Store;
void check(bool good,const char* why) {
  if (good) return;
  std::fprintf(stderr,"Engine directory: %s\n",why); std::abort();
}
template<class T> T take(orbital::Result<T> r) {
  check(bool(r),"unexpected module refusal"); return std::move(*r);
}
directory::Dirent value(std::uint32_t key) {
  directory::Dirent d;
  for (unsigned i=0;i<6;++i) d.parts[i]=(UINT64_C(0xeab91200)+key)*17+i;
  d.representation=key^0xffu; d.mapped=0x1234; d.extra_rank=key;
  d.extra_block=key^UINT32_MAX; d.reserved=0x4567;
  return d;
}
void publish(Store& store,const Store::Scope& basis,std::span<const Store::Change> changes) {
  Store::Prepared packet(store);
  check(bool(store.prepare(basis,changes,packet)),"prepare");
  check(bool(store.install(packet)),"install");
}
void run(loom::Worker& worker) {
  std::pmr::unsynchronized_pool_resource resource({1024,128});
  Store store(resource),foreign(resource);
  auto directory=take(directory::Directory::open(store,worker,resource,UINT64_C(0xfedcba9876543210)));
  { Store::Scope empty(store),other(foreign);
    auto read=take(directory->read(empty,4));
    check(!read.find(0),"absent root");
    directory::Dirent dummy;
    const std::array keys{0u,UINT32_MAX}; std::array<const directory::Dirent*,2> output{&dummy,&dummy};
    check(bool(read.find_many(keys,output)) && !output[0] && !output[1],"empty many output");
    check(!directory->read(other,4) && !directory->read(empty,5),"foreign Scope/layer"); }
  std::vector<directory::Entry> entries;
  for (unsigned i=0;i<1300;++i) entries.push_back({i*17,value(i*17)});
  entries.push_back({UINT32_MAX-1,value(UINT32_MAX-1)});
  entries.push_back({UINT32_MAX,value(UINT32_MAX)});
  auto owner=take(directory->build(4,entries));
  { Store::Scope basis(store);
    const std::array changes{Store::Change{directory->root_id(4),std::move(owner)}};
    publish(store,basis,changes); }
  std::optional<Store::Scope> old; old.emplace(store);
  // Do not bind old root until AFTER all replacements, candidate destruction,
  // catalog maintenance and fresh reads. This checks late first discovery.
  const std::array keys{0u,17u,16*17u,32*17u,1299*17u,UINT32_MAX-1,UINT32_MAX};
  for (auto key:keys) {
    Store::Scope basis(store);
    auto d=value(key); d.parts[5]^=0x9999; d.representation^=0xabc;
    directory::Work work;
    auto next=take(directory->replace(basis,4,key,d,&work));
    check(work.allocations==1 && work.captures==1,
        "replacement physical allocation/capture bound");
    check(work.body_bytes==work.copied_bytes && work.body_bytes<=4480,
        "bounded path backing/copy accounting");
    const std::array changes{Store::Change{directory->root_id(4),std::move(next)}};
    publish(store,basis,changes);
  }
  store.drain();
  {
    Store::Scope fresh(store);
    auto before=take(directory->read(*old,4)),after=take(directory->read(fresh,4));
    for (const auto& e:entries) {
      auto expected=e.value;
      for (auto key:keys) if (key==e.prefix) { expected.parts[5]^=0x9999; expected.representation^=0xabc; }
      check(before.find(e.prefix) && *before.find(e.prefix)==e.value,"old full dirent bytes");
      check(after.find(e.prefix) && *after.find(e.prefix)==expected,"fresh full dirent bytes");
    }
    for (unsigned key=0;key<30000;++key)
      if (key%17 || key>1299*17) check(!before.find(key) && !after.find(key),"sparse exact misses");
    for (unsigned count:{0u,1u,31u,32u,33u,63u,64u,65u}) {
      std::array<std::uint32_t,65> probes;
      std::array<const directory::Dirent*,66> results{};
      for (unsigned i=0;i<count;++i) probes[i]=i%5==0?UINT32_MAX:i%5==1?16u:(i*17)%1000;
      check(bool(before.find_many(std::span(probes).first(count),results)),"many old probes");
      for (unsigned i=0;i<count;++i) check(results[i]==before.find(probes[i]),"old many input association");
      check(!results[count],"many wrote past offered count");
      check(bool(after.find_many(std::span(probes).first(count),results)),"many fresh probes");
      for (unsigned i=0;i<count;++i) check(results[i]==after.find(probes[i]),"fresh many input association");
      if (count) {
        auto saved=results;
        check(!after.find_many(std::span(probes).first(count),std::span(results).first(count-1)),"many capacity refusal");
        check(results==saved,"many refusal changed output");
      }
    }
    check(!directory->replace(fresh,4,16,value(16)),"absent replacement admitted");
    auto discarded=take(directory->replace(fresh,4,17,value(0))); discarded={};
    check(*take(directory->read(fresh,4)).find(17)==*after.find(17),"discard published");
    auto candidate=take(directory->replace(fresh,4,17,value(0)));
    Store::Prepared aborted(store);
    const std::array changes{Store::Change{directory->root_id(4),std::move(candidate)}};
    check(bool(store.prepare(fresh,changes,aborted)),"abort preparation"); aborted.abort();
    check(*take(directory->read(fresh,4)).find(17)==*after.find(17),"abort published");
  }
  old.reset(); store.drain();
  std::vector<directory::Entry> dense;
  for (unsigned i=0;i<1024;++i) dense.push_back({i,value(i)});
  {
    auto root=take(directory->build(2,dense)); Store::Scope basis(store);
    const std::array c{Store::Change{directory->root_id(2),std::move(root)}}; publish(store,basis,c);
  }
  {
    Store::Scope before(store);
    auto expected=dense;
    for (unsigned stride:{1u,15u}) {
      std::array<directory::LocatorChange,64> batch;
      for (unsigned i=0;i<64;++i) {
        const auto key=i*stride; auto d=value(key); d.parts[2]^=stride;
        batch[i]={key,d}; expected[key].value=d;
      }
      Store::Scope basis(store); directory::Work work;
      auto root=take(directory->replace_many(basis,2,batch,&work));
      check(work.captures==1 && work.body_bytes>=work.copied_bytes,"batch once per physical body");
      if (stride==1) check(work.allocations==6 && work.copied_bytes==4*1024+2*576,"clustered union path");
      const std::array c{Store::Change{directory->root_id(2),std::move(root)}}; publish(store,basis,c);
    }
    store.drain(); Store::Scope fresh(store);
    auto a=take(directory->read(before,2)),b=take(directory->read(fresh,2));
    for (unsigned i=0;i<dense.size();++i)
      check(a.find(i) && *a.find(i)==dense[i].value && b.find(i) && *b.find(i)==expected[i].value,"batch old/fresh all bytes");
    const std::array bad_batch{directory::LocatorChange{1,value(1)},directory::LocatorChange{1,value(2)}};
    check(!directory->replace_many(fresh,2,bad_batch),"duplicate final intent admitted");
  }
  // Layer domains, compressed roots, empty setup and sorted/unique admission.
  for (unsigned layer=0;layer<4;++layer) {
    auto end=std::uint32_t((UINT64_C(1)<<(8*layer))-1);
    std::array input{directory::Entry{end,value(end)}};
    auto root=take(directory->build(layer,input));
    Store::Scope basis(store);
    const std::array changes{Store::Change{directory->root_id(layer),std::move(root)}};
    publish(store,basis,changes);
    Store::Scope fresh(store); auto read=take(directory->read(fresh,layer));
    check(read.find(end) && *read.find(end)==input[0].value,"compressed/layer maximum");
    check(!read.find(end+1),"outside-layer lookup");
    input[0].prefix=end+1;
    check(!directory->build(layer,input) && !directory->replace(fresh,layer,end+1,value(0)),"layer key domain");
  }
  std::array bad{directory::Entry{7,value(7)},directory::Entry{7,value(7)}};
  check(!directory->build(4,bad) && !directory->build(4,{}) && !directory->build(5,entries),"build input admission");
  {
    Store::Scope basis(store); auto read=take(directory->read(basis,4)); const auto original=*read.find(0);
    const std::array invalid{directory::LocatorChange{0,value(0)},directory::LocatorChange{16,value(16)},directory::LocatorChange{17,value(17)}};
    check(!directory->replace_many(basis,4,invalid),"partial private batch membership refusal");
    check(*take(directory->read(basis,4)).find(0)==original,"refused partial batch published");
  }
  // Escaped root Part preserves routing descendants after Directory/Store reset.
  orbital::Part<material::Control>* exported;
  {
    Store::Scope basis(store);
    exported=take(take(store.bound(basis,directory->root_id(4))).export_part(0,orbital::PartRetention::selected_copy));
  }
  directory.reset(); store={};
  check(orbital::view(exported).address()!=nullptr,"exported graph lifetime");
  orbital::release(exported);
  // Force every admitted radix height. Expectations come from flat input,
  // independently of either scalar find or the staged traversal.
  Store shapes(resource);
  auto shaped=take(directory::Directory::open(shapes,worker,resource,9876));
  for (unsigned shift=0;shift<=25;shift+=5) {
    const auto high=std::uint32_t(1u<<(shift+4));
    const std::array input{directory::Entry{0,value(0)},directory::Entry{high,value(high)}};
    auto root=take(shaped->build(4,input));
    { Store::Scope basis(shapes); const std::array c{Store::Change{shaped->root_id(4),std::move(root)}}; publish(shapes,basis,c); }
    Store::Scope basis(shapes); auto read=take(shaped->read(basis,4));
    std::array<std::uint32_t,97> keys; std::array<const directory::Dirent*,97> output;
    for (unsigned i=0;i<97;++i) keys[i]=i%4==0?0:i%4==1?high:i%4==2?high+1:UINT32_MAX;
    check(bool(read.find_many(keys,output)),"staged radix height");
    for (unsigned i=0;i<keys.size();++i) {
      const directory::Dirent* expected=nullptr;
      for (const auto& e:input) if (e.prefix==keys[i]) expected=&e.value;
      check(bool(output[i])==bool(expected) && (!expected || *output[i]==*expected),"staged independent flat oracle");
    }
  }
}
}
int main() {
  loom::Worker worker; check(bool(loom::adopt(worker)),"adopt");
  check(bool(loom::configure(worker,take(orbital::allocation::open_arena(64u<<20)),0)),"configure");
  run(worker);
  for (;;) { auto p=loom::shutdown(worker); check(p.kind()!=orbital::Progress::Kind::failed,"shutdown");
    if (p.kind()==orbital::Progress::Kind::done) break; loom::poll(worker,{64,0}); }
  loom::relinquish(worker);
  std::puts("PASS: native physical directory full-byte oracle, sparse/MAX/layer domains, late old/fresh snapshots, discard/abort, bounded single-path backing and physical lifetime");
}
