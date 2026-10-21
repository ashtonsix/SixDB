#include <engine/material.h>
#include <loom/worker.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace {
using namespace engine::material;
void check(bool v,const char* message) {
  if (v) return;
  std::fprintf(stderr,"Engine material: %s\n",message); std::abort();
}
template<class T> T take(orbital::Result<T> r) {
  check(bool(r),"unexpected module refusal"); return std::move(*r);
}
struct Tracker { unsigned* destroyed; ~Tracker() { ++*destroyed; } };
void run(loom::Worker& worker) {
  static_assert(!std::is_copy_constructible_v<Layout> && !std::is_move_constructible_v<Layout>);
  std::pmr::unsynchronized_pool_resource resource;
  const auto space=UINT64_C(0xfedcba9876543210),name=UINT64_C(0xffffffffffffffff);
  const std::array geometry{Geometry{64},Geometry{64}};
  unsigned destroyed=0;
  auto proof=std::make_shared<Tracker>(&destroyed);
  auto definition=take(Definition::native(resource,space,7,geometry,proof)); proof.reset();
  Store store(resource),recaptured(resource);
  auto allocation=take(loom::allocate(worker,{.bytes=128,.alignment=64}));
  for (unsigned i=0;i<128;++i) allocation.address[i]=std::byte(i^0xa5);
  const auto control=definition.control(name,orbital::PartOwners::grouped_allocation);
  const std::array sources{
    orbital::PartSource{control.id(0),{allocation.address,64},{},&allocation},
    orbital::PartSource{control.id(1),{allocation.address+64,64},{},&allocation}};
  auto owner=take(store.capture(control,sources)); check(!allocation,"grouped source consumed once");
  {
    Store::Scope basis(store); Store::Prepared packet(store);
    const std::array changes{Store::Change{{space,name},owner}};
    check(bool(store.prepare(basis,changes,packet)) && bool(store.install(packet)),"publish original");
  }
  std::optional<Store::Scope> old; old.emplace(store);
  const auto bound=take(store.bound(*old,{space,name}));
  auto* escaped=take(bound.export_part(1,orbital::PartRetention::selected_copy));
  orbital::retain(escaped); orbital::release(escaped); // ordinary-header link surface
  const auto association=orbital::binding(escaped);
  check(association.object==orbital::ObjectId{space,name} && association.version==1,
      "export lost full Object binding");
  owner={}; definition={}; store={};
  check(!destroyed && old->bind({space,name}).part_count()==2,"late Scope lost Layout after handle reset");
  const auto view=orbital::view(escaped);
  for (unsigned i=0;i<64;++i) check(view.address()[i]==std::byte((i+64)^0xa5),"selected Part bytes");
  auto replacement=take(Definition::native(resource,space,8,std::span(geometry).first(1)));
  auto source=take(orbital::source(escaped));
  const auto next=replacement.control(17); source.id=next.id(0);
  const std::array kept{source};
  auto finished=take(recaptured.capture(next,kept));
  old.reset(); orbital::release(escaped);
  check(destroyed==1,"retained native backing kept predecessor interpretation alive");
  check(take(finished.selection().part(0)).address()[0]==std::byte(64^0xa5),"recaptured physical source lost bytes");
  const std::array bad_changes{Store::Change{{space^1,17},finished}};
  Store::Scope basis(recaptured); Store::Prepared refused(recaptured);
  check(!recaptured.prepare(basis,bad_changes,refused),"namespace relabel accepted");
  std::pmr::unsynchronized_pool_resource wrong_resource;
  auto wrong=take(Definition::native(wrong_resource,space,9,std::span(geometry).first(1)));
  auto fresh=take(loom::allocate(worker,{.bytes=64,.alignment=64}));
  std::memset(fresh.address,0,64);
  auto wrong_control=wrong.control(99);
  const std::array fresh_source{orbital::PartSource{wrong_control.id(0),loom::bytes(fresh),{},&fresh}};
  check(!recaptured.capture(wrong_control,fresh_source) && bool(fresh),"resource refusal consumed allocation");
  wrong_control=replacement.control(99); wrong_control.first=UINT32_MAX; wrong_control.projected=1;
  check(!recaptured.capture(wrong_control,fresh_source) && bool(fresh),"invalid projection admitted");
  auto zero=take(Definition::native(resource,space,10,{}));
  auto empty=take(recaptured.capture(zero.control(23),{}));
  check(empty.selection().part_count()==0 && !empty.selection().part(0),"zero-Part geometry");
}
}
int main() {
  loom::Worker worker; check(bool(loom::adopt(worker)),"Worker adoption");
  auto arena=take(orbital::allocation::open_arena(16u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configuration");
  run(worker);
  for (;;) {
    auto p=loom::shutdown(worker);
    check(p.kind()!=orbital::Progress::Kind::failed,"physical shutdown");
    if (p.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::puts("PASS: Engine Layout survives handle/reset/export, native recapture drops predecessor proof, full identity/resource/projection/zero-Part checks and shutdown");
}
