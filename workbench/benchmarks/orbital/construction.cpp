#include "packed-construction.h"
#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>
namespace p=orbital_bench::packed;
namespace a=orbital::allocation;
void require(bool ok,const char* why) {
  if(!ok) {std::fprintf(stderr,"blank construction: %s\n",why);std::abort();}
}
template<class T,class E> T take(std::expected<T,E> r) {
  require(bool(r),"runtime admission failed");return std::move(*r);
}
int main() {
  loom::Worker worker;
  require(bool(loom::adopt(worker)),"adopt failed");
  auto arena=take(a::open_arena(64u<<20));
  require(bool(loom::configure(worker,arena,0)),"configure failed");
  p::Schema schema;
  for(bool helper:{false,true}) for(unsigned trial=0;trial<8;++trial) {
    auto page=std::make_unique<p::Page>();
    a::AllocationRequest request;
    request.bytes=p::page_bytes;request.reserved_bytes=p::page_bytes+64;
    request.alignment=64;request.readable_padding=64;
    request.initialization=a::Initialization::uninitialized;
    auto backing=take(loom::allocate(worker,request));
    auto* base=backing.address;
    const auto body=std::span<std::byte>{base,backing.size};
    const auto padding=backing.readable_padding;
    require(std::all_of(base+p::page_bytes,base+p::page_bytes+64,
                       [](auto byte){return byte==std::byte{0};}),"runtime padding not initialized");
    std::fill_n(base,p::page_bytes,std::byte{0xa5});
    std::fill_n(base+p::page_bytes,64,std::byte{0x3c});
    auto held=take(loom::hold(backing,{0,p::page_bytes+64}));
    if(!trial) {
      bool rejected=false;
      try {(void)p::construct_blank_page(*page,schema,body,0,true);}
      catch(const std::runtime_error&) {rejected=true;}
      require(rejected && !page->bindings &&
              std::all_of(base,base+p::page_bytes,[](auto b){return b==std::byte{0xa5};}),
              "padding admission did not reject before body writes/binding publication");
    }
    p::ConstructionDelta delta;
    if(helper) {
      std::exception_ptr failure;
      // The assigned caller owns Allocation throughout; helper retains physical
      // backing and constructs bytes only. This is no Pool ownership transfer.
      std::thread thread([&, retained=std::move(held)]() mutable {
        require(!orbital::worker::on_lane(worker.lane),"helper unexpectedly owns caller lane");
        try {delta=p::construct_blank_page(*page,schema,body,padding,true);}
        catch(...) {failure=std::current_exception();}
        a::release(retained);
      });
      thread.join(); if(failure)std::rethrow_exception(failure);
    } else delta=p::construct_blank_page(*page,schema,body,padding,true);
    require(std::all_of(base,base+p::page_bytes,[](auto b){return b==std::byte{0};}),
            "constructor left nonzero body bytes from poisoned prior contents");
    require(std::all_of(base+p::page_bytes,base+p::page_bytes+64,
                       [](auto b){return b==std::byte{0x3c};}),"constructor wrote outside body");
    require(page->live==0 && page->sum==0,"blank visibility/summary differs");
    require(delta.issued_write_bytes==p::series_bytes+p::capacity*p::tuple_stride &&
            delta.issued_write_spans==2 && delta.index_write_bytes==p::page_bytes-p::order_offset &&
            delta.index_write_spans==1,"local issued accounting differs from independent extents");
    auto& b=*page->bindings;
    for(std::size_t slot=0;slot<p::capacity;++slot) {
      require(b.values.get(slot)==0 && b.keys.get(slot)==0 && b.next.get(slot)==0 && b.tags.get(slot)==0,
              "blank fields not fully initialized");
      require(page->order[slot]==0 && page->occupied[slot/64]==0,"blank index not initialized");
    }
    require(!b.values.get(p::capacity) && !b.keys.get(p::capacity),"capacity proof lost row bounds");
    require(!page->allocation,"construction required/published an Allocation handle");
    // Only the assigned caller publishes the real Allocation into ready Page.
    page->allocation=std::move(backing);
    a::release(held);page.reset();loom::poll(worker,{1024,0});
  }
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  for(;;) {
    const auto status=loom::shutdown(worker);
    if(status.kind()==orbital::Progress::Kind::done)break;
    require(status.kind()!=orbital::Progress::Kind::failed &&
            std::chrono::steady_clock::now()<deadline,"shutdown failed/stuck");
    loom::poll(worker,{1024,0});std::this_thread::yield();
  }
  require(arena->charged_bytes==0,"Arena charge remained after helper/inline construction");
  loom::relinquish(worker);
  std::puts("blank construction: helper+inline, retained backing, poisoned body, padding bounds, local effects and zero-charge shutdown pass");
}
