#include <engine/directory.h>
#include <loom/worker.h>
#include "logical_oracle.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace {
namespace d=engine::directory;
namespace model=index_spike::logical_oracle;
using Store=engine::material::Store;
void check(bool good,const char* why) { if (good) return; std::fprintf(stderr,"logical Directory: %s\n",why); std::abort(); }
template<class T> T take(orbital::Result<T> r) { check(bool(r),"module refusal"); return std::move(*r); }
d::Dirent payload(const model::Entry& e) {
  d::Dirent value; std::memcpy(&value,e.payloads[0].data(),64); return value;
}
void publish(Store& store,const Store::Scope& basis,std::span<const Store::Change> changes) {
  Store::Prepared packet(store); check(bool(store.prepare(basis,changes,packet)),"prepare");
  check(bool(store.install(packet)),"install");
}
void install(d::Directory& directory,Store& store,const model::State& state) {
  std::array<Store::Change,5> changes;
  for (unsigned layer=0;layer<5;++layer) {
    std::vector<d::Entry> entries;
    for (const auto& [id,e]:state) if (id.first==layer) entries.push_back({id.second,payload(e),e.minimum,e.maximum});
    changes[layer]={directory.root_id(layer),{}};
    if (!entries.empty()) changes[layer].representation=take(directory.build(layer,entries));
  }
  Store::Scope basis(store); publish(store,basis,changes);
}
void query(const d::LogicalRead& read,const model::State& state,unsigned layer,
    std::optional<model::Bytes> low,std::optional<model::Bytes> high,bool lc,bool hc,unsigned& cases) {
  const auto expected=model::overlap(state,layer,low,high,lc,hc);
  d::Range range; if (low) range.low=std::span<const std::byte>(*low); if (high) range.high=std::span<const std::byte>(*high);
  range.low_closed=lc; range.high_closed=hc;
  d::Cursor cursor; check(bool(read.seek(range,cursor)),"logical seek");
  std::array<d::EntryRef,7> output;
  check(cursor.visit(std::span(output).first(0))==0,"empty output advanced cursor");
  std::size_t position=0;
  for (;;) {
    const auto n=cursor.visit(output); if (!n) break;
    for (unsigned i=0;i<n;++i) {
      check(position<expected.size() && output[i].prefix==expected[position].second,"ordered full-key/duplicate association");
      check(*output[i].value==payload(state.at(expected[position])),"logical dirent full64 bytes"); ++position;
    }
  }
  check(position==expected.size(),"logical result cardinality");
  d::Dirent dummy; d::EntryRef canary{123,&dummy}; check(!cursor.next(canary) && canary.prefix==123 && canary.value==&dummy,"exhausted cursor output");
  ++cases;
}
void verify(d::Directory& directory,const Store::Scope& scope,const model::State& state,unsigned& cases) {
  for (unsigned layer=0;layer<5;++layer) {
    auto read=take(directory.logical(scope,layer)); query(read,state,layer,{},{},true,false,cases);
    unsigned index=0;
    for (const auto& [id,e]:state) if (id.first==layer) {
      // Every run and storage boundary, plus all open/closed endpoint choices.
      for (bool lc:{false,true}) for (bool hc:{false,true}) {
        query(read,state,layer,e.minimum,e.maximum,lc,hc,cases);
        query(read,state,layer,e.maximum,e.maximum,lc,hc,cases);
      }
      if (!(index++%17)) {
        query(read,state,layer,{},e.minimum,true,true,cases);
        query(read,state,layer,e.maximum,{},false,false,cases);
        query(read,state,layer,e.maximum,e.minimum,true,true,cases);
      }
    }
    // Compare the short equality entry against the independent overlap oracle,
    // including K12 equal-abbreviation runs and repeated fences.
    for (const auto& [id,e]:state) if (id.first==layer) {
      d::Cursor equal; check(bool(read.equal(e.maximum,equal)),"equality entry");
      const auto expected=model::overlap(state,layer,e.maximum,e.maximum,true,true);
      d::EntryRef output; unsigned n=0;
      while (equal.next(output)) {
        check(n<expected.size() && output.prefix==expected[n].second &&
            *output.value==payload(state.at(expected[n])),"equality full-key oracle"); ++n;
      }
      check(n==expected.size(),"equality cardinality");
    }
    // Refused width must preserve a previously prepared continuation.
    d::Cursor cursor; check(bool(read.seek({},cursor)),"prepare refusal canary");
    model::Bytes wrong(99); d::Range bad; bad.low=std::span<const std::byte>(wrong);
    check(!read.seek(bad,cursor),"wrong width accepted");
    const auto expected=model::prefixes(state,layer);
    d::EntryRef output{}; const auto found=cursor.next(output);
    check(found==!expected.empty() && (!found || output.prefix==expected[0].second),"refusal lost old continuation");
  }
}
void run(loom::Worker& worker) {
  unsigned snapshots=0,cases=0;
  for (unsigned width:{8u,12u}) {
    std::pmr::unsynchronized_pool_resource memory({128,2048}); Store store(memory);
    auto directory=take(d::Directory::open(store,worker,memory,99,d::KeyShape{width,2}));
    model::State before;
    for (const auto& batch:model::batches(width)) {
      const auto after=model::apply(before,batch.edits,width);
      Store::Scope old(store); install(*directory,store,after); store.drain();
      // Neither old root nor its bounds was selected before the new publication.
      verify(*directory,old,before,cases);
      { Store::Scope fresh(store); verify(*directory,fresh,after,cases); }
      before=after; ++snapshots;
    }
    // Locator-only COW retains all admitted immutable bounds. Its new payload
    // and old/fresh bounds are checked independently of full-snapshot builds.
    auto selected=before.begin();
    while (selected!=before.end() && selected->first.first!=4) ++selected;
    check(selected!=before.end(),"fixture missing L4");
    const auto id=selected->first;
    Store::Scope old(store); auto replacement=payload(selected->second); replacement.parts[2]^=UINT64_C(0xdeadbeef);
    auto root=take(directory->replace(old,id.first,id.second,replacement));
    { const std::array changes{Store::Change{directory->root_id(id.first),std::move(root)}}; publish(store,old,changes); }
    verify(*directory,old,before,cases);
    Store::Scope fresh(store); auto logical=take(directory->logical(fresh,id.first));
    d::Cursor cursor; d::Range r; r.low=std::span<const std::byte>(selected->second.minimum); r.high=std::span<const std::byte>(selected->second.maximum); r.high_closed=true;
    check(bool(logical.seek(r,cursor)),"replacement seek"); bool found=false; d::EntryRef e;
    while (cursor.next(e)) if (e.prefix==id.second) { check(*e.value==replacement,"replacement payload"); found=true; }
    check(found,"replacement lost logical bounds");
    auto kept=take(directory->logical(old,4)); directory.reset(); store={};
    query(kept,before,4,{},{},true,false,cases); // cached shape + Scope outlive handle
  }
  // J validation, order and byte-width refusal happen before body construction.
  {
    std::pmr::unsynchronized_pool_resource memory; Store store(memory);
    check(!d::Directory::open(store,worker,memory,44,d::KeyShape{8,7}) && !d::Directory::open(store,worker,memory,44,d::KeyShape{0,1}),"invalid shared width");
    auto directory=take(d::Directory::open(store,worker,memory,44,d::KeyShape{8,0}));
    const auto a=model::key(8,{4,0},20),b=model::key(8,{4,0},10);
    std::array input{d::Entry{0,{},a,a},d::Entry{1,{},b,b}}; d::Work work;
    check(!directory->build(4,input,&work) && !work.allocations,"out of order admitted");
  }
  // K=0 is an equal-key domain, distinct from physical-only mode.
  {
    std::pmr::unsynchronized_pool_resource memory; Store store(memory);
    auto directory=take(d::Directory::open(store,worker,memory,77,d::KeyShape{0,0}));
    const std::array input{d::Entry{0,{}},d::Entry{UINT32_MAX,{}}};
    auto root=take(directory->build(4,input));
    { Store::Scope basis(store); const std::array c{Store::Change{directory->root_id(4),std::move(root)}}; publish(store,basis,c); }
    Store::Scope basis(store); auto read=take(directory->logical(basis,4)); d::Cursor cursor;
    std::span<const std::byte> empty; d::Range r{empty,empty,true,true};
    check(bool(read.seek(r,cursor)),"zero-key equality"); std::array<d::EntryRef,3> output; check(cursor.visit(output)==2,"zero-key duplicates");
    r.high_closed=false; check(bool(read.seek(r,cursor)) && cursor.visit(output)==0,"zero-key open endpoint");
  }
  std::printf("PASS: logical directory %u independent snapshots/%u ranges, K8/K12 equal-abbreviation repair, all layers, duplicates/open/closed/unbounded, late old/fresh, retained bounds/handle lifetime, width/order/J admission and K0\n",snapshots,cases);
}
}
int main() {
  loom::Worker worker; check(bool(loom::adopt(worker)),"adopt"); check(bool(loom::configure(worker,take(orbital::allocation::open_arena(128u<<20)),0)),"configure");
  run(worker);
  for (;;) { auto p=loom::shutdown(worker); check(p.kind()!=orbital::Progress::Kind::failed,"shutdown"); if (p.kind()==orbital::Progress::Kind::done) break; loom::poll(worker,{64,0}); }
  loom::relinquish(worker);
}
