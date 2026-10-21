#include <engine/directory_hash.h>
#include <ikea/hashtable/lookup.h>
#include <loom/worker.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>
namespace {
using namespace engine;
using Store=material::Store;
void check(bool b,const char* why) { if (b) return; std::fprintf(stderr,"Hash directory: %s\n",why); std::abort(); }
template<class T> T take(orbital::Result<T> r) { check(bool(r),"module refusal"); return std::move(*r); }
directory::Dirent value(unsigned key) {
  directory::Dirent d; auto* p=reinterpret_cast<unsigned char*>(&d);
  for (unsigned i=0;i<64;++i) p[i]=(key*17+i*23)&255;
  if (!key) d={}; return d;
}
void publish(Store& store,const Store::Scope& basis,std::span<const Store::Change> changes) {
  Store::Prepared packet(store); check(bool(store.prepare(basis,changes,packet)),"prepare"); check(bool(store.install(packet)),"install");
}
void run(loom::Worker& worker) {
  std::pmr::unsynchronized_pool_resource memory;
  Store store(memory),foreign(memory);
  auto directory=take(directory::Directory::open(store,worker,memory,177,directory::KeyShape{8,4}));
  check(!directory->hashed(0) && !directory->hashed(1) && !directory->hashed(5),"hash recipe admission");
  { directory::HashRead unbound; check(!unbound.group(0) && !unbound.group(UINT32_MAX),"unbound read"); }
  for (unsigned layer=2;layer<5;++layer) {
    auto hash=take(directory->hashed(layer));
    Store::Scope absent(store),other(foreign);
    directory::HashRead empty; check(bool(hash->read(absent,empty)),"empty read");
    check(!take(empty.group(0)).get(0).found,"absent root hit");
    check(!hash->read(other,empty),"foreign Scope admitted");
    std::vector<std::array<std::byte,8>> keys; keys.reserve(65);
    std::vector<directory::Entry> entries;
    const auto maximum=layer==4?UINT32_MAX:unsigned((UINT64_C(1)<<(layer*8))-1);
    for (unsigned i=0;i<65;++i) {
      const auto prefix=i==64?maximum:i*17;
      std::array<std::byte,8> key{};
      for (unsigned b=0;b<layer;++b) key[b]=std::byte(prefix>>(8*(layer-1-b)));
      keys.push_back(key); entries.push_back({prefix,value(prefix),keys.back(),keys.back()});
    }
    auto initial=take(hash->initialize(absent,entries)); check(initial.size()==66,"one group owner per selected group plus root");
    publish(store,absent,initial); initial.clear();
    directory::HashRead old_empty; check(bool(hash->read(absent,old_empty)),"late absent root");
    check(!take(old_empty.group(0)).get(0).found,"old absent discovers new group");
    std::optional<Store::Scope> old; old.emplace(store);
    { Store::Scope basis(store);
      auto a=value(17); a.parts[4]^=0x6677; auto b=value(34); b.reserved^=0x99;
      const std::array changes{directory::LocatorChange{17,a},directory::LocatorChange{34,b}};
      auto finished=take(hash->replace(basis,changes)); check(finished.size()==2,"existing update replaced header/extra group");
      publish(store,basis,finished);
    }
    store.drain();
    Store::Scope fresh(store);
    directory::HashRead before,after; check(bool(hash->read(*old,before)) && bool(hash->read(fresh,after)),"read snapshots");
    for (const auto& entry:entries) {
      auto a=take(before.group(entry.prefix>>4)).get(entry.prefix&15),b=take(after.group(entry.prefix>>4)).get(entry.prefix&15);
      directory::Dirent old_value,new_value; std::memcpy(&old_value,a.value.data(),64); std::memcpy(&new_value,b.value.data(),64);
      auto expected=entry.value; if (entry.prefix==17) expected.parts[4]^=0x6677; if (entry.prefix==34) expected.reserved^=0x99;
      check(a.found && b.found && old_value==entry.value && new_value==expected,"old/fresh full64 including zero hit");
    }
    for (unsigned key=0;key<1200;++key) if (key%17 || key>63*17) {
      auto selected=take(after.group(key>>4)).get(key&15); check(!selected.found,"sparse miss");
      for (auto byte:selected.value) check(byte==0,"missing value not zero");
    }
    check(!after.group(UINT32_MAX),"out of domain group");
    check(!hash->initialize(fresh,entries),"initialize over installed root");
    const std::array bad{directory::LocatorChange{17,value(8)},directory::LocatorChange{18,value(9)}};
    check(!hash->replace(fresh,bad),"partial group refusal");
    const std::array one{directory::LocatorChange{17,value(7)}};
    { auto discarded=take(hash->replace(fresh,one)); }
    { auto candidate=take(hash->replace(fresh,one)); Store::Prepared packet(store);
      check(bool(store.prepare(fresh,candidate,packet)),"abort prepare"); packet.abort(); }
    auto unchanged=take(after.group(1)).get(1); directory::Dirent d; std::memcpy(&d,unchanged.value.data(),64);
    auto expected=value(17); expected.parts[4]^=0x6677; check(d==expected,"discard/abort published");
    // Persistent selection drives real group get plus every value word and hit
    // reduction, across many caller blocks and selector learning boundaries.
    ikea::hashtable::lookup_selector selector;
    std::uint64_t actual_sum=0,expected_sum=0,actual_hits=0,expected_hits=0;
    for (unsigned call=0;call<197;++call) {
      selector.run(64,[&](auto hint,std::size_t first,std::size_t count) {
        for (auto i=first;i<first+count;++i) {
          const auto key=unsigned((call*64+i)%1200);
          auto result=take(after.group(key>>4)).template get<decltype(hint)::value>(key&15);
          std::array<std::uint64_t,8> words; std::memcpy(words.data(),result.value.data(),64);
          for (auto x:words) actual_sum+=x; actual_hits+=result.found;
        }
      });
      for (unsigned i=0;i<64;++i) {
        const auto key=(call*64+i)%1200;
        if (key%17 || key>63*17) continue;
        auto expected=value(key); if (key==17) expected.parts[4]^=0x6677; if (key==34) expected.reserved^=0x99;
        std::array<std::uint64_t,8> words; std::memcpy(words.data(),&expected,64);
        for (auto x:words) expected_sum+=x; ++expected_hits;
      }
    }
    const auto stats=selector.statistics();
    check(actual_sum==expected_sum && actual_hits==expected_hits,"automatic complete full64 consumer oracle");
    check(stats.predictable && stats.unpredictable && stats.predictable+stats.unpredictable==197*64 && stats.probes>=8,"selector reset/lost work");
    old.reset();
  }
  {
    // Dense bitmap occupancy, one clone for a same-group batch, and actual
    // independently versioned Segment material joined with locator publication.
    auto dense=take(directory::Directory::open(store,worker,memory,178,directory::KeyShape{8,2}));
    auto hash=take(dense->hashed(2));
    std::array<std::array<std::byte,8>,32> keys{};
    std::array<directory::Entry,32> entries;
    for (unsigned i=0;i<32;++i) {
      keys[i][1]=std::byte(i); entries[i]={i,value(i),keys[i],keys[i]};
    }
    const std::array geometry{material::Geometry{64}};
    auto definition=take(material::Definition::native(memory,179,902,geometry));
    const orbital::ObjectId segment{179,1};
    auto material=[&](unsigned byte) {
      auto allocation=take(loom::allocate(worker,{.bytes=64,.alignment=64})); std::memset(allocation.address,byte,64);
      const auto control=definition.control(segment.low);
      const std::array source{orbital::PartSource{control.id(0),loom::bytes(allocation),{},&allocation}};
      return take(store.capture(control,source));
    };
    {
      Store::Scope basis(store); auto owner=material(0x37);
      entries[0].value.parts[0]=std::uintptr_t(owner.selection().part(0)->address());
      auto initial=take(hash->initialize(basis,entries)); check(initial.size()==3,"dense group count");
      initial.push_back({segment,std::move(owner)}); publish(store,basis,initial);
    }
    Store::Scope old(store);
    {
      Store::Scope basis(store); auto owner=material(0xe2);
      std::array<directory::LocatorChange,16> batch;
      for (unsigned i=0;i<16;++i) { auto d=entries[i].value; d.parts[5]^=0x5533; batch[i]={i,d}; }
      batch[0].value.parts[0]=std::uintptr_t(owner.selection().part(0)->address());
      auto changed=take(hash->replace(basis,batch)); check(changed.size()==1,"same group copied multiple times");
      changed.push_back({segment,std::move(owner)}); publish(store,basis,changed);
    }
    store.drain(); Store::Scope fresh(store);
    for (const auto* scope:{&old,&fresh}) {
      directory::HashRead read; check(bool(hash->read(*scope,read)),"coupled read");
      for (unsigned i=0;i<32;++i) {
        const auto result=take(read.group(i>>4)).get(i&15); check(result.found,"dense membership");
        directory::Dirent d; std::memcpy(&d,result.value.data(),64);
        auto expected=entries[i].value; if (scope==&fresh && i<16) expected.parts[5]^=0x5533;
        if (!i) {
          const auto* bytes=reinterpret_cast<const std::byte*>(std::uintptr_t(d.parts[0]));
          check(bytes==scope->bind(segment).part(0)->address(),"locator/Segment snapshot mismatch");
          for (unsigned b=0;b<64;++b) check(bytes[b]==std::byte(scope==&old?0x37:0xe2),"coupled Segment bytes");
          expected.parts[0]=d.parts[0];
        }
        check(d==expected,"dense same-group neighbors changed");
      }
    }
  }
  // Context owns no extra reader protocol: Scope alone preserves late group
  // discovery after both the factory and Store handles have been released.
  std::optional<Store::Scope> scope; scope.emplace(store);
  directory::HashRead surviving; { auto hash=take(directory->hashed(2)); check(bool(hash->read(*scope,surviving)),"surviving read"); }
  directory.reset(); store={}; auto result=take(surviving.group(1)).get(1); check(result.found,"Scope reset lifetime"); scope.reset();
}
}
int main() {
  loom::Worker worker; check(bool(loom::adopt(worker)),"adopt"); check(bool(loom::configure(worker,take(orbital::allocation::open_arena(64u<<20)),0)),"configure");
  run(worker);
  for (;;) { auto p=loom::shutdown(worker); check(p.kind()!=orbital::Progress::Kind::failed,"shutdown"); if (p.kind()==orbital::Progress::Kind::done) break; loom::poll(worker,{64,0}); }
  loom::relinquish(worker); std::puts("PASS: actual Ikea hash directory full64/zero hit/miss, late old/fresh Scope, group-only clone, discard/abort/refusal, persistent automatic complete consumer");
}
