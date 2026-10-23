#include <orbital/preparation.h>
#include "internal.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <vector>
using namespace orbital;
namespace alloc = orbital::allocation;

namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace {
thread_local bool owner_thread{};
std::atomic<bool> gate{}, inside{}, proceed{}, fail_map{}, fail_protect{}, fail_unmap{};
std::atomic<unsigned> helper_maps{}, owner_maps{}, helper_protects{}, owner_protects{};
std::atomic<unsigned> helper_constructions{}, cleanup_failures{}, cleanup_skip{};
std::atomic<bool> fail_metadata{}, metadata_reservation_seen{};
std::atomic<std::size_t> metadata_size{};
std::atomic<void*> failed_protection_address{};
std::atomic<std::size_t> failed_protection_bytes{};

void check(bool yes, const char* text, std::source_location loc = std::source_location::current()) {
  if (!yes) { std::fprintf(stderr,"%s:%u: %s\n",loc.file_name(),loc.line(),text); std::abort(); }
}

template<class T, class E> T take(std::expected<T,E> value) { check(bool(value),"operation failed"); return std::move(*value); }

void done(Result<void> value, std::source_location loc = std::source_location::current()) { if(!value)std::fprintf(stderr,"error=%u\n",unsigned(value.error())); check(bool(value),"void operation failed",loc); }

auto deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(10); }

void wait_inside() {
  const auto limit = deadline();
  while (!inside.load(std::memory_order_acquire)) { check(std::chrono::steady_clock::now()<limit,"helper not at gate"); std::this_thread::yield(); }
}

struct PreparerService {
  worker::Lane lane;
  worker::Thread thread;
  std::atomic<bool> ready{}, stopping{};
  std::atomic<int> actual_cpu{-1};
  int cpu{-1};
  std::uint64_t startup_ns{}, join_ns{};
  std::atomic<std::uint64_t> service_cpu_ns{};
  PreparerService() {
    cpu_set_t available; CPU_ZERO(&available);
    check(::sched_getaffinity(0,sizeof(available),&available)==0,"preparer CPU inventory");
    const auto consumer=::sched_getcpu();
    for(int candidate=0;candidate<CPU_SETSIZE;++candidate)
      if(CPU_ISSET(candidate,&available) && candidate!=consumer){cpu=candidate;break;}
    if(cpu==-1)cpu=consumer;
    const auto begin=std::chrono::steady_clock::now();
    done(worker::spawn(thread,entry,this,cpu));
    const auto limit=deadline();
    while(!ready.load(std::memory_order_acquire)){
      check(std::chrono::steady_clock::now()<limit,"preparer startup readiness"); std::this_thread::yield();
    }
    startup_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
  }
  static void entry(void* context)noexcept{
    auto& self=*static_cast<PreparerService*>(context);
    const auto cpu_clock=[] {
      timespec value{}; check(::clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)==0,"helper CPU clock");
      return std::uint64_t(value.tv_sec)*1000000000+std::uint64_t(value.tv_nsec);
    };
    const auto cpu_begin=cpu_clock();
    done(worker::adopt(self.lane,self.cpu));
    self.actual_cpu=::sched_getcpu(); self.ready.store(true,std::memory_order_release);
    for(;;){
      worker::poll(self.lane,1);
      if(self.stopping.load(std::memory_order_acquire) && !worker::pending(self.lane))break;
      std::this_thread::yield();
    }
    worker::relinquish(self.lane);
    self.service_cpu_ns=cpu_clock()-cpu_begin;
  }
  void stop(){
    if(!thread.native.joinable())return;
    const auto begin=std::chrono::steady_clock::now();
    stopping.store(true,std::memory_order_release); done(worker::join(thread));
    join_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    check(!worker::pending(lane),"preparer route retained outbound message");
  }
  ~PreparerService(){stop();}
};

struct Fixture {
  worker::Lane lane;
  Ref<alloc::Arena> arena;
  PreparerService preparer;
  alloc::Pool* pool{};
  Fixture(std::size_t limit=32*1024*1024) {
    owner_thread=true;
    done(worker::adopt(lane));
    arena=take(alloc::open_arena(limit));
    pool=take(alloc::add_pool(*arena,lane));
  }
  void settle(alloc::Preparation& request) {
    const auto limit=deadline();
    while (request.state==alloc::Preparation::State::pending) {
      check(std::chrono::steady_clock::now()<limit,"preparation did not settle");
      worker::poll(lane,32); std::this_thread::yield();
    }
    check(request.state==alloc::Preparation::State::repair_wait || !request.backend,"completed request retained backend");
  }
  void close_all(bool permit_failure=false) {
    const auto limit=deadline(); bool observed_failure=false;
    for (;;) {
      auto progress=alloc::close(*pool);
      if (progress.kind()==Progress::Kind::failed) {
        check(permit_failure && progress.failure()==Error::io,"close failed unexpectedly");
        observed_failure=true;
        check(arena->charged_bytes.load()>0,"failed reclamation refunded before unmap");
      }
      if (progress.kind()==Progress::Kind::done) break;
      check(std::chrono::steady_clock::now()<limit,"close did not settle");
      worker::poll(lane,32); std::this_thread::yield();
    }
    if(permit_failure)check(observed_failure,"expected failure absent");
    check(arena->charged_bytes.load()==0,"close retained charged bytes");
    done(alloc::remove_pool(*pool)); pool=nullptr;
  }
  ~Fixture() {
    if(pool)close_all();
    check(!worker::pending(lane),"close retained queued Message");
    worker::relinquish(lane); owner_thread=false;
  }
};

std::size_t base_page() { return std::size_t(::sysconf(_SC_PAGESIZE)); }

std::size_t page_round(std::size_t bytes) {
  const auto page=base_page();
  return ((bytes+page-1)/page)*page;
}

alloc::AllocationRequest page_request() {
  return {6304,std::max(std::size_t{65536},4*base_page()),64,64,alloc::Initialization::zero};
}

std::size_t prepared_bytes(const alloc::AllocationRequest& request=page_request()) {
  return page_round(request.bytes+request.readable_padding);
}

std::size_t reserved_bytes(const alloc::AllocationRequest& request=page_request()) {
  return page_round(std::max(request.bytes+request.readable_padding,request.reserved_bytes));
}

std::size_t one_window_budget() { return prepared_bytes()+sizeof(alloc::detail::Window); }

void arm_gate() { proceed=false; inside=false; gate=true; }

void paused_cancel_close() {
  Fixture f; alloc::Preparation request;
  const auto maps=owner_maps.load(), protects=owner_protects.load();
  arm_gate(); done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); wait_inside();
  const auto charge=f.arena->charged_bytes.load();
  check(charge>prepared_bytes(),"private Window not fully charged before VM");
  check(!alloc::take(request),"unprepared backing consumable");
  auto stats=alloc::statistics(*f.pool);
  check(stats.mapped_bytes==0 && stats.reserved_bytes==0 && stats.preparing_bytes==prepared_bytes() &&
        stats.preparing_reserved_bytes==reserved_bytes() && stats.idle_bytes==0,
        "pending budget misreported as prepared backing or actual reserved VA");
  check(charge==stats.metadata_bytes+stats.preparing_bytes,"pending charge relation");
  unsigned handled=0;
  const worker::MessageOps ops{
    [](worker::Message&)noexcept{},
    [](worker::Message& m,worker::Lane&)noexcept{++*static_cast<unsigned*>(m.context);},
    [](worker::Message&)noexcept{}};
  worker::Message unrelated; unrelated.context=&handled; unrelated.ops=&ops;
  worker::post(f.lane,unrelated);
  check(worker::poll(f.lane,1)==1 && handled==1,"paused helper blocked unrelated owner completion");
  alloc::cancel(request);
  check(request.state==alloc::Preparation::State::pending && request.backend,"cancel destroyed physical operation");
  check(alloc::close(*f.pool).kind()==Progress::Kind::waiting,"close passed pending VM job");
  check(f.arena->charged_bytes.load()==charge,"cancel/close refunded before completion");
  proceed=true; f.settle(request);
  check(request.state==alloc::Preparation::State::cancelled,"late cancellation delivered allocation");
  check(!request.value,"cancelled request owns visible allocation");
  check(owner_maps.load()==maps && owner_protects.load()==protects,"VM ran on consumer");
  f.close_all();
  std::puts("{\"case\":\"paused-cancel-close\",\"passed\":true}");
}

void ready_owner_semantics() {
  Fixture f; alloc::Preparation request;
  const auto maps=owner_maps.load(), protects=owner_protects.load();
  done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); f.settle(request);
  auto value=take(alloc::take(request));
  check(!alloc::take(request),"preparation delivered twice");
  check(owner_maps.load()==maps && owner_protects.load()==protects,"VM ran on consumer");
  check(reinterpret_cast<std::uintptr_t>(value.address)%64==0,"alignment not preserved");
  for(std::size_t i=0;i<value.size+value.readable_padding;++i)check(value.address[i]==std::byte{0},"zero/padding missing");
  auto lease=take(alloc::hold(value,{0,value.size}));
  std::thread foreign([&]{
    const auto grown=alloc::grow(value,{7000,page_request().reserved_bytes,64,64});
    check(!grown && grown.error()==Error::conflict,"foreign grow accepted prepared owner backing");
    const auto held=alloc::hold(value,{0,8});
    check(!held && held.error()==Error::conflict,"foreign hold accepted prepared owner backing");
  }); foreign.join();
  done(alloc::grow(value,{7000,page_request().reserved_bytes,64,64}));
  alloc::release(lease); alloc::release(value);
  std::puts("{\"case\":\"owner-grow-hold-exactly-once\",\"passed\":true}");
}

void mapping_failure() {
  Fixture f; alloc::Preparation request;
  fail_map=true; done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); f.settle(request);
  check(request.state==alloc::Preparation::State::failed && request.error==Error::io,"mmap failure result lost");
  check(f.arena->charged_bytes.load()==0,"mmap failure retained nonexistent mapping charge");
  std::puts("{\"case\":\"mmap-failure\",\"passed\":true}");
}

void fresh_failure_moves_pending_window() {
  Fixture f;
  auto left=take(alloc::allocate(*f.pool,page_request()));
  auto right=take(alloc::allocate(*f.pool,page_request()));
  std::memset(left.address,0x5d,left.size); std::memset(right.address,0x6e,right.size);
  const auto base_charge=f.arena->charged_bytes.load();
  alloc::Preparation refused,later;
  fail_map=true; arm_gate();
  done(alloc::prepare(*f.pool,refused,page_request(),f.preparer.lane)); wait_inside();
  // Later stable backend is published while the earlier one will fail. Owner
  // removes the middle record while the later Message is still in transit.
  done(alloc::prepare(*f.pool,later,page_request(),f.preparer.lane));
  alloc::cancel(refused); proceed=true;
  f.settle(refused); f.settle(later);
  check(refused.state==alloc::Preparation::State::cancelled && !refused.backend && !refused.owner,
        "cancelled failed fresh mapping retained backend");
  auto value=take(alloc::take(later));
  check(f.arena->charged_bytes.load()==base_charge+prepared_bytes()+alloc::statistics(*f.pool).metadata_bytes/3,
        "middle failure did not refund exactly one Window");
  for(auto byte:alloc::bytes(value))check(byte==std::byte{0},"pending moved Window lost constructor result");
  for(auto byte:alloc::bytes(left))check(byte==std::byte{0x5d},"middle failure damaged left live mapping");
  for(auto byte:alloc::bytes(right))check(byte==std::byte{0x6e},"middle failure damaged right live mapping");
  alloc::release(value); alloc::release(left); alloc::release(right);
  f.close_all();
  std::puts("{\"case\":\"fresh-failure-moves-pending-window\",\"passed\":true}");
}

void uncertain_failure_retry() {
  Fixture f; alloc::Preparation request;
  fail_protect=true; done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); f.settle(request);
  check(request.state==alloc::Preparation::State::failed && request.error==Error::io,"partial mprotect failure result lost");
  const auto charge=f.arena->charged_bytes.load();
  const auto uncertain=alloc::statistics(*f.pool);
  check(charge>prepared_bytes() && uncertain.mapped_bytes==0 && uncertain.uncertain_bytes==prepared_bytes() &&
        uncertain.reserved_bytes==reserved_bytes() && charge==uncertain.metadata_bytes+uncertain.uncertain_bytes,
        "partial mapping uncertainty misreported as confirmed ready payload");
  auto fallback=take(alloc::allocate(*f.pool,page_request()));
  check(f.arena->charged_bytes.load()==charge,"uncertain mapping retry charged twice");
  for(auto byte:alloc::bytes(fallback))check(byte==std::byte{0},"fallback not initialized");
  alloc::release(fallback);
  fail_unmap=true; f.close_all(true);
  std::puts("{\"case\":\"uncertain-mprotect-retry-unmap-failure\",\"passed\":true}");
}

void terminal_records_after_pool_replacement() {
  Fixture f; alloc::Preparation failed, cancelled, taken;
  fail_map=true;
  done(alloc::prepare(*f.pool,failed,page_request(),f.preparer.lane)); f.settle(failed);
  done(alloc::prepare(*f.pool,cancelled,page_request(),f.preparer.lane)); f.settle(cancelled);
  alloc::cancel(cancelled);
  done(alloc::prepare(*f.pool,taken,page_request(),f.preparer.lane)); f.settle(taken);
  auto value=take(alloc::take(taken)); alloc::release(value);
  f.close_all();
  f.pool=take(alloc::add_pool(*f.arena,f.lane));
  for(unsigned round=0;round<5;++round){
    const auto error=alloc::take(failed);
    check(!error && error.error()==Error::io,"failed terminal take consulted removed Pool");
    const auto gone=alloc::take(cancelled);
    check(!gone && gone.error()==Error::rejected,"cancelled terminal take consulted removed Pool");
    const auto consumed=alloc::take(taken);
    check(!consumed && consumed.error()==Error::invalid_argument,"taken terminal state reopened after Pool replacement");
    alloc::cancel(failed); alloc::cancel(cancelled); alloc::cancel(taken);
  }
  check(!failed.owner && !cancelled.owner && !taken.owner,"terminal record retains former Pool");
  std::puts("{\"case\":\"terminal-records-after-pool-replacement\",\"passed\":true}");
}

void uninitialized_prefault() {
  Fixture f; alloc::Preparation request;
  auto wanted=page_request(); wanted.initialization=alloc::Initialization::uninitialized;
  wanted.readable_padding=0;
  const auto protects=owner_protects.load();
  done(alloc::prepare(*f.pool,request,wanted,f.preparer.lane)); f.settle(request);
  auto value=take(alloc::take(request));
  std::vector<unsigned char> resident(prepared_bytes(wanted)/base_page());
  check(::mincore(value.address,prepared_bytes(wanted),resident.data())==0,"mincore prepared pages");
  check(std::all_of(resident.begin(),resident.end(),[](auto status){return status&1;}),
      "uninitialized request left foreground first-touch faults");
  check(owner_protects.load()==protects,"uninitialized preparation used owner VM");
  alloc::release(value);
  std::puts("{\"case\":\"uninitialized-no-padding-prefault\",\"passed\":true}");
}

void bounded_charge_refusal() {
  Fixture f(one_window_budget()); alloc::Preparation first,second;
  arm_gate(); done(alloc::prepare(*f.pool,first,page_request(),f.preparer.lane)); wait_inside();
  const auto before=f.arena->charged_bytes.load();
  check(before==f.arena->byte_limit,"one Window did not consume its exact bounded charge");
  const auto refused=alloc::prepare(*f.pool,second,page_request(),f.preparer.lane);
  check(!refused && refused.error()==Error::capacity && second.state==alloc::Preparation::State::idle,
      "refusal accepted or mutated caller frame");
  check(f.arena->charged_bytes.load()==before,"refusal changed charge");
  alloc::cancel(first); proceed=true; f.settle(first);
  std::puts("{\"case\":\"bounded-inflight-charge-refusal\",\"passed\":true}");
}

using Format=sp::preset_layout<20,sp::preset::compact>;
constexpr std::size_t rows=256, series_bytes=640, tuple_offset=640, tuple_stride=20;

struct Constructor {
  tp::layout layout;
  tp::write_plan<128> tuple;
  std::uint64_t written_bytes{};
  std::size_t issued_spans{}, completions{};
  Constructor() : layout([]{
    std::array<tp::code,tuple_stride> codes;
    for(std::size_t i=0;i<codes.size();++i)codes[i]={std::uint8_t(i),0,8};
    return take(tp::layout::make(tuple_stride,codes));
  }()),tuple(take(tp::write_plan<128>::make(layout))){}
  static Result<void> initialize(alloc::PreparedBytes destination,void* context)noexcept{
    const auto storage=destination.body;
    check(destination.readable_padding==64,"constructor did not receive readable padding contract");
    for(std::size_t i=0;i<destination.readable_padding;++i)
      check(storage.data()[storage.size()+i]==std::byte{0},"constructor padding not initialized");
    auto& own=*static_cast<Constructor*>(context);
    if(!owner_thread)++helper_constructions;
    auto series=take(sp::view<Format>::attach(rows, {{{storage.first(series_bytes),Format::tile_bytes},{},{}}}));
    auto prepared=take(sp::bind_writer(series));
    auto write=prepared.erase<std::uint32_t,sp::author::sum_change>();
    auto tuples=take(tp::view::attach(own.layout,
        storage.subspan(tuple_offset,rows*tuple_stride),rows,tuple_stride));
    auto construct=take(tp::bind_writer(own.tuple,tuples));
    std::array<std::uint32_t,rows> values{};
    for(std::size_t i=0;i<rows;++i)values[i]=std::uint32_t(11*i+5);
    std::array<tp::packet<128>,rows> records{};
    for(std::size_t i=0;i<rows;++i){ records[i][0]=std::uint8_t(i); records[i][8]=std::uint8_t(255-i); }
    std::array<ikea::journal::entry,rows*4> effects_storage;
    ikea::journal effects{effects_storage};
    check(bool(write.initialize(values,effects)),"SeriesPack initialize");
    const auto capture=[&]{
      for(const auto& effect:effects.entries()){
        const auto limit=effect.source==&series ? series_bytes :
                         effect.source==&tuples ? rows*tuple_stride : 0;
        check(limit && !effect.plane && effect.offset<=limit && effect.size<=limit-effect.offset,
            "constructor effect not source-qualified");
        own.written_bytes+=effect.size; ++own.issued_spans;
      }
      effects.used=0;
    };
    capture(); check(bool(construct.initialize(0,records,effects)),"TuplePack initialize"); capture();
    return {};
  }
  void verify(MutableBytes storage){
    auto series=take(sp::view<Format>::attach(rows, {{{storage.first(series_bytes),Format::tile_bytes},{},{}}}));
    auto decoder=sp::bind_reader<std::uint32_t>(series);
    auto tuples=take(tp::view::attach(layout,
        storage.subspan(tuple_offset,rows*tuple_stride),rows,tuple_stride));
    const std::array<std::uint8_t,8> map{0,1,2,3,4,5,6,7};
    auto reader=take(tp::read_plan<8>::make(layout,map));
    auto read=take(tp::bind_reader(reader,tuples));
    for(std::size_t i=0;i<rows;++i){
      check(decoder.get(i)==11*i+5,"prepared SeriesPack contents");
      check(read.get(i)==i,"prepared TuplePack contents");
      check(storage[tuple_offset+i*tuple_stride+8]==std::byte(255-i),"prepared TuplePack secondary field");
    }
    ++completions;
  }
};

void cancelled_constructor_and_same_fallback() {
  Fixture f; alloc::Preparation request; Constructor build;
  request.construct=Constructor::initialize; request.context=&build;
  const auto before=helper_constructions.load();
  arm_gate(); done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); wait_inside();
  alloc::cancel(request); check(alloc::close(*f.pool).kind()==Progress::Kind::waiting,"close skipped cancelled constructor");
  proceed=true; f.settle(request);
  check(helper_constructions.load()==before+1 && build.written_bytes>0 && build.completions==0,
      "physical constructor abandoned or cancelled result merged");
  f.close_all();
  f.pool=take(alloc::add_pool(*f.arena,f.lane));
  auto fallback=take(alloc::allocate(*f.pool,page_request()));
  Constructor local;
  done(Constructor::initialize({alloc::bytes(fallback),fallback.readable_padding},&local)); local.verify(alloc::bytes(fallback));
  check(local.written_bytes==build.written_bytes && local.issued_spans==build.issued_spans,
      "fallback changed constructor/effect accounting");
  alloc::release(fallback);
  std::puts("{\"case\":\"cancelled-real-constructor-and-same-fallback\",\"passed\":true}");
}

void explicit_preparer_reclaim_cancel_close() {
  Fixture f; auto& service=f.preparer; alloc::Preparation request;
  auto old=take(alloc::allocate(*f.pool,page_request())); alloc::release(old);
  const auto old_charge=f.arena->charged_bytes.load();
  auto incoming=page_request(); incoming.reserved_bytes*=2;
  arm_gate(); done(alloc::prepare(*f.pool,request,incoming,service.lane)); wait_inside();
  const auto both=f.arena->charged_bytes.load();
  check(both==2*old_charge,"explicit Lane pending charge mismatch");
  check(alloc::trim_cold(*f.pool,0).kind()==Progress::Kind::waiting,"unrelated trim not scheduled");
  const auto limit=deadline();
  while(f.arena->charged_bytes.load()==both){
    check(std::chrono::steady_clock::now()<limit,"separate preparer blocked Arena reclamation");
    worker::poll(f.lane,32); std::this_thread::yield();
  }
  check(f.arena->charged_bytes.load()==old_charge,"unrelated retirement did not refund exactly once");
  alloc::cancel(request);
  check(alloc::close(*f.pool).kind()==Progress::Kind::waiting,"Pool closed before outstanding preparer returned");
  service.stopping=true; // Stops accepting external posts; accepted work still drains.
  proceed=true; f.settle(request); service.stop(); f.close_all();
  check(!request.preparer && !request.owner,"terminal record retained either former route");
  alloc::cancel(request); check(!alloc::take(request),"terminal cancel/take reopened removed Pool");
  std::printf("{\"case\":\"explicit-preparer-reclamation-progress-cancel-close\",\"passed\":true,\"reclaim_progress_while_paused\":true,\"preparer_cpu_requested\":%d,\"preparer_cpu_actual\":%d,\"extra_service_threads_in_fixture\":1,\"cold_startup_ns\":%llu,\"service_thread_cpu_ns\":%llu,\"explicit_service_join_ns\":%llu,\"service_loop\":\"busy-yield-fixture-no-performance-claim\"}\n",
      service.cpu,service.actual_cpu.load(),(unsigned long long)service.startup_ns,
      (unsigned long long)service.service_cpu_ns.load(),(unsigned long long)service.join_ns);
}

void explicit_preparer_real_format_and_owner_operations() {
  Fixture f; auto& service=f.preparer; alloc::Preparation request; Constructor construct;
  request.construct=Constructor::initialize; request.context=&construct;
  const auto owner_vm=owner_maps.load()+owner_protects.load();
  done(alloc::prepare(*f.pool,request,page_request(),service.lane)); f.settle(request);
  check(owner_vm==owner_maps.load()+owner_protects.load(),"explicit route did foreground VM work");
  auto ready=take(alloc::take(request));
  auto lease=take(alloc::hold(ready,{0,ready.size}));
  construct.verify(alloc::bytes(ready)); done(alloc::grow(ready,{7000,page_request().reserved_bytes,64,64}));
  alloc::release(lease); alloc::release(ready);
  service.stop();
  alloc::Preparation invalid;
  const auto refused=alloc::prepare(*f.pool,invalid,page_request(),f.lane);
  check(!refused && refused.error()==Error::invalid_argument && invalid.state==alloc::Preparation::State::idle,
      "same-thread route accepted as background preparation");
  std::puts("{\"case\":\"explicit-preparer-real-format-owner-hold-grow\",\"passed\":true}");
}

struct GatedConstructor {
  Constructor constructor;
  static Result<void> initialize(alloc::PreparedBytes destination,void* context)noexcept{
    auto& self=*static_cast<GatedConstructor*>(context);
    if(gate.exchange(false)){
      inside.store(true,std::memory_order_release);
      while(!proceed.load(std::memory_order_acquire))std::this_thread::yield();
    }
    return Constructor::initialize(destination,&self.constructor);
  }
};

void idle_reuse_exclusive_without_vm() {
  Fixture f(one_window_budget()); alloc::Preparation request; GatedConstructor builder;
  auto old=take(alloc::allocate(*f.pool,page_request())); auto* address=old.address;
  alloc::release(old); const auto charge=f.arena->charged_bytes.load();
  const auto vm_calls=owner_maps+helper_maps+owner_protects+helper_protects;
  request.construct=GatedConstructor::initialize; request.context=&builder;
  arm_gate(); done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); wait_inside();
  const auto stats=alloc::statistics(*f.pool);
  check(request.reused && stats.mapped_bytes==prepared_bytes() && stats.preparing_bytes==0 &&
      stats.idle_bytes==0 && stats.reserved_bytes==reserved_bytes() && f.arena->charged_bytes==charge,
      "known backing/charge not preserved or pending reused Window exposed as idle");
  const auto refused=alloc::allocate(*f.pool,page_request());
  check(!refused && refused.error()==Error::capacity,"pending Window was allocated to a second owner");
  check(alloc::trim_cold(*f.pool,0).kind()==Progress::Kind::waiting,"pending constructor was reclaimed");
  proceed=true; f.settle(request);
  auto ready=take(alloc::take(request));
  check(ready.address==address && f.arena->charged_bytes==charge,"idle reuse changed address or double charged");
  check(vm_calls==owner_maps+helper_maps+owner_protects+helper_protects,"fully prepared reuse issued VM syscall");
  builder.constructor.verify(alloc::bytes(ready));
  check(alloc::statistics(*f.pool).reuses==1,"reused prepared delivery not counted exactly once");
  alloc::release(ready);
  std::puts("{\"case\":\"idle-window-exclusive-constructor-no-vm\",\"passed\":true}");
}

void reused_window_partial_growth_and_retry() {
  Fixture f; alloc::Preparation failed, retry;
  auto old=take(alloc::allocate(*f.pool,page_request())); auto* address=old.address;
  std::memset(old.address,0x5a,old.size); alloc::release(old);
  auto larger=page_request();
  larger.bytes=std::max(std::size_t{20000},prepared_bytes()+2*base_page()-larger.readable_padding);
  const auto prefix=prepared_bytes(), complete=prepared_bytes(larger);
  const auto maps=owner_maps+helper_maps;
  fail_protect=true; done(alloc::prepare(*f.pool,failed,larger,f.preparer.lane)); f.settle(failed);
  const auto uncertainty=alloc::statistics(*f.pool);
  const auto charge=f.arena->charged_bytes.load();
  check(failed.reused && failed.state==alloc::Preparation::State::failed && failed.error==Error::io,
      "reused partial preparation failure lost");
  check(uncertainty.mapped_bytes==prefix && uncertainty.uncertain_bytes==complete-prefix &&
      uncertainty.preparing_bytes==0 && charge==uncertainty.mapped_bytes+uncertainty.uncertain_bytes+uncertainty.metadata_bytes,
      "reused known prefix/uncertain suffix charge was lost or double counted");
  for(std::size_t i=0;i<6304;++i)check(address[i]==std::byte{0x5a},"failed suffix protection corrupted prepared prefix");
  done(alloc::prepare(*f.pool,retry,larger,f.preparer.lane)); f.settle(retry);
  auto ready=take(alloc::take(retry));
  check(retry.reused && ready.address==address && f.arena->charged_bytes==charge,"retry lost identity or charged twice");
  check(maps==owner_maps+helper_maps,"reused protection retry remapped reservation");
  const auto confirmed=alloc::statistics(*f.pool);
  check(confirmed.mapped_bytes==complete && confirmed.uncertain_bytes==0,"retry did not resolve conservative suffix charge");
  for(std::size_t i=0;i<ready.size+ready.readable_padding;++i)check(ready.address[i]==std::byte{0},"retry constructor/zero padding");
  alloc::release(ready);
  std::puts("{\"case\":\"reused-window-partial-growth-same-reservation-retry\",\"passed\":true}");
}

void repeated_idle_prepare_cancel_cycles() {
  Fixture f; std::byte* address{}; std::uint64_t charge{};
  const auto initial_maps=helper_maps.load();
  for(unsigned round=0;round<24;++round){
    alloc::Preparation request;
    done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); f.settle(request);
    if(!round){ address=request.value.address; charge=f.arena->charged_bytes.load(); }
    check(request.value.address==address && f.arena->charged_bytes==charge,"idle cycles grew mapping inventory/charge");
    alloc::cancel(request);
    check(request.state==alloc::Preparation::State::cancelled && !request.owner && !request.backend,
        "ready cancel retained request/Pool ownership");
  }
  check(helper_maps.load()==initial_maps+1,"repeated idle readiness recreated mappings");
  check(alloc::statistics(*f.pool).reuses==23,"reuse count lost across cancellation cycles");
  std::puts("{\"case\":\"repeated-idle-ready-cancel-reuse\",\"passed\":true}");
}

void constructed_delivery_and_unread_ready() {
  Fixture f; alloc::Preparation request; Constructor build;
  request.construct=Constructor::initialize; request.context=&build;
  done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); f.settle(request);
  auto page=take(alloc::take(request)); build.verify(alloc::bytes(page));
  check(build.written_bytes>=series_bytes+rows*tuple_stride && build.issued_spans>0 && build.completions==1,
      "local constructor effects missing");
  alloc::release(page);
  alloc::Preparation unread;
  done(alloc::prepare(*f.pool,unread,page_request(),f.preparer.lane)); f.settle(unread);
  check(alloc::close(*f.pool).kind()==Progress::Kind::waiting,"close skipped unread ready allocation");
  alloc::cancel(unread); f.close_all();
  std::puts("{\"case\":\"real-ikea-construction-and-unread-ready\",\"passed\":true}");
}

alloc::AllocationRequest small_request(std::size_t bytes=64,std::size_t alignment=64){
  return {bytes,bytes,alignment,0,alloc::Initialization::zero};
}

struct SmallConstructor {
  bool gated{}, fail{}; unsigned calls{}; std::byte* address{};
  static Result<void> run(alloc::PreparedBytes bytes,void* context)noexcept{
    auto& self=*static_cast<SmallConstructor*>(context); ++self.calls; self.address=bytes.body.data();
    if(self.gated && gate.exchange(false)){
      inside.store(true,std::memory_order_release);
      while(!proceed.load(std::memory_order_acquire))std::this_thread::yield();
    }
    std::memset(bytes.body.data(),0x42,bytes.body.size());
    return self.fail ? Result<void>{std::unexpected(Error::rejected)} : Result<void>{};
  }
};

void finite_fresh_bounded_start_cancel_close(){
  Fixture f; alloc::Preparation first, second;
  const auto ownervm=owner_maps+owner_protects;
  arm_gate(); done(alloc::prepare(*f.pool,first,small_request(),f.preparer.lane));wait_inside();
  const auto charge=f.arena->charged_bytes.load(); const auto inventory=alloc::statistics(*f.pool);
  const auto palette=alloc::detail::preferred_palette(base_page(),64,64);
  const auto planned=take(alloc::detail::FiniteGroup::create({palette,256}));
  check(inventory.mapped_bytes==0 && inventory.preparing_reserved_bytes==
        planned->payload_pages()*planned->native_page() && inventory.metadata_bytes==charge,
        "fresh control preparation includes only payload VA and separate metadata charge");
  for(unsigned i=0;i<20;++i){
    const auto refused=alloc::prepare(*f.pool,second,small_request(),f.preparer.lane);
    check(!refused && refused.error()==Error::unavailable && second.state==alloc::Preparation::State::idle,
          "fresh finite burst created another pending Group");
    check(f.arena->charged_bytes==charge,"deferred control request grew inventory");
  }
  alloc::cancel(first);check(alloc::close(*f.pool).kind()==Progress::Kind::waiting,"close passed private control work");
  proceed=true;f.settle(first);check(first.state==alloc::Preparation::State::cancelled,"late control cancel delivered");
  check(ownervm==owner_maps+owner_protects,"control VM used consumer");
  f.close_all();alloc::cancel(first);check(!alloc::take(first),"terminal finite reopened removed Pool");
  std::printf("{\"case\":\"finite-fresh-control-bounded-burst-cancel-close\",\"passed\":true,\"one_pending_group_charge\":%llu}\n",(unsigned long long)charge);
}

void finite_control_cancel_reopens_palette(){
  Fixture f; alloc::Preparation first, other, next;
  const auto wanted=small_request(64,16), distinct=small_request(3072,64);
  const bool two_palettes=alloc::detail::preferred_palette(base_page(),wanted.bytes,wanted.alignment)!=
      alloc::detail::preferred_palette(base_page(),distinct.bytes,distinct.alignment);
  arm_gate();done(alloc::prepare(*f.pool,first,wanted,f.preparer.lane));wait_inside();
  // One palette's accepted control work must not block the other palette.
  if(two_palettes)done(alloc::prepare(*f.pool,other,distinct,f.preparer.lane));
  const auto charge=f.arena->charged_bytes.load();
  auto refused=alloc::prepare(*f.pool,next,wanted,f.preparer.lane);
  check(!refused && refused.error()==Error::unavailable && next.state==alloc::Preparation::State::idle &&
        f.arena->charged_bytes==charge,"same-palette refusal changed accepted control inventory");
  alloc::cancel(first);
  refused=alloc::prepare(*f.pool,next,wanted,f.preparer.lane);
  check(!refused && refused.error()==Error::unavailable,
        "cancellation released control admission before physical settlement");
  proceed=true;f.settle(first);
  check(first.state==alloc::Preparation::State::cancelled,"cancelled control delivered an Allocation");
  alloc::Allocation independent;
  if(two_palettes){f.settle(other);independent=take(alloc::take(other));}
  done(alloc::prepare(*f.pool,next,wanted,f.preparer.lane));f.settle(next);
  auto value=take(alloc::take(next));
  for(auto byte:alloc::bytes(value))check(byte==std::byte{0},"replacement control omitted initialized bytes");
  alloc::release(value);alloc::release(independent);f.close_all();
  std::puts("{\"case\":\"finite-control-palette-cancel-settle-retry\",\"passed\":true}");
}

void finite_control_failures_own_partial_backing(){
  for(const auto wanted:{small_request(64,16),small_request(64,64),small_request(256,64)}){
  for(bool partial:{false,true}){
    Fixture f;alloc::Preparation failed;
    const auto references=f.arena->references.load();
    const auto expected=partial?Error::capacity:Error::io;
    // Metadata heap ENOMEM happens after the real sparse payload reservation.
    const auto palette=alloc::detail::preferred_palette(base_page(),wanted.bytes,wanted.alignment);
    auto planned=take(alloc::detail::FiniteGroup::create({palette,256}));
    metadata_size=planned->control_bytes();metadata_reservation_seen=false;
    if(partial)fail_metadata=true;else fail_map=true;
    done(alloc::prepare(*f.pool,failed,wanted,f.preparer.lane));f.settle(failed);
    const auto inventory=alloc::statistics(*f.pool);const auto charged=f.arena->charged_bytes.load();
    check(failed.state==alloc::Preparation::State::failed && failed.error==expected && !failed.backend &&
          !failed.owner && inventory.mapped_bytes==0 && inventory.preparing_bytes==0 &&
          inventory.uncertain_bytes==0 && inventory.allocations==0 && !failed.value &&
          inventory.metadata_bytes==charged && charged>0,
          "failed finite control lost terminal status or partial mapping custody");
    check((inventory.reserved_bytes>0)==partial,"failed finite control reported nonexistent/lost sparse mapping");
    check(!fail_metadata && (metadata_reservation_seen.load()==partial),"heap failure did not follow real payload reservation");
    check(f.arena->references.load()==references,"failed helper retained its preparation pin");
    alloc::Preparation recovery;done(alloc::prepare(*f.pool,recovery,wanted,f.preparer.lane));f.settle(recovery);
    auto healthy=take(alloc::take(recovery));alloc::release(healthy);
    if(partial){fail_unmap=true;f.close_all(true);}else f.close_all();
    f.pool=take(alloc::add_pool(*f.arena,f.lane));alloc::cancel(failed);
    const auto result=alloc::take(failed);check(!result && result.error()==expected,
          "terminal failed finite frame consulted replaced Pool");
  }
  }
  std::puts("{\"case\":\"finite-control-heap-ENOMEM-payload-unmap-retry\",\"passed\":true}");
}

void finite_delivered_owner_operations(){
  Fixture f;alloc::Preparation request;SmallConstructor constructor;
  request.construct=SmallConstructor::run;request.context=&constructor;
  const auto ownervm=owner_maps+owner_protects;
  done(alloc::prepare(*f.pool,request,small_request(),f.preparer.lane));f.settle(request);
  auto allocation=take(alloc::take(request));check(constructor.calls==1,"finite constructor replay");
  check(ownervm==owner_maps+owner_protects,"finite VM used consumer");
  for(auto byte:alloc::bytes(allocation))check(byte==std::byte{0x42},"finite constructor bytes");
  auto held=take(alloc::hold(allocation,{0,64}));
  auto* original=allocation.address;
  const bool fits=alloc::reserved_bytes(allocation)>=96;
  const auto grown=alloc::grow(allocation,{96,96,64,0});
  if(fits)done(grown);
  else check(!grown && grown.error()==Error::conflict && allocation.address==original && allocation.size==64,
      "prepared finite growth relocated live captured bytes");
  for(auto byte:alloc::bytes(held))check(byte==std::byte{0x42},"held finite prefix changed during growth");
  std::thread foreign([&]{check(!alloc::hold(allocation,{0,1}),"foreign finite hold accepted");});foreign.join();
  alloc::release(held);
  if(!fits)done(alloc::grow(allocation,{96,96,64,0}));
  for(std::size_t i=0;i<64;++i)check(allocation.address[i]==std::byte{0x42},"quiescent finite growth lost prefix");
  alloc::release(allocation);
  std::puts("{\"case\":\"finite-full-vm-constructor-owner-hold-grow\",\"passed\":true}");
}

void finite_reuse_uninitialized_prefault(){
  Fixture f;auto wanted=small_request();wanted.initialization=alloc::Initialization::uninitialized;
  auto prior=take(alloc::allocate(*f.pool,wanted));auto* address=prior.address;
  const auto page=std::size_t(::sysconf(_SC_PAGESIZE));
  auto* base=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(address)&~(page-1));
  unsigned char resident{};check(::mincore(base,page,&resident)==0 && !(resident&1),"legacy uninitialized request unexpectedly resident");
  alloc::release(prior);alloc::Preparation request;
  done(alloc::prepare(*f.pool,request,wanted,f.preparer.lane));f.settle(request);
  auto value=take(alloc::take(request));check(value.address==address && request.reused,"uninitialized ready Home not reused");
  check(::mincore(base,page,&resident)==0 && (resident&1),"mapped reuse left foreground first-touch fault");
  alloc::release(value);
  std::puts("{\"case\":\"finite-mapped-uninitialized-reuse-prefault\",\"passed\":true}");
}

void finite_mapped_neighbour_cancel_paused_constructor(){
  Fixture f;auto neighbour=take(alloc::allocate(*f.pool,small_request()));std::memset(neighbour.address,0x5a,64);
  alloc::Preparation request;SmallConstructor constructor;constructor.gated=true;
  request.construct=SmallConstructor::run;request.context=&constructor;
  arm_gate();done(alloc::prepare(*f.pool,request,small_request(),f.preparer.lane));wait_inside();
  auto unrelated=take(alloc::allocate(*f.pool,small_request()));
  check(unrelated.address!=neighbour.address && unrelated.address!=constructor.address,"pending Home reused");
  alloc::cancel(request);check(alloc::close(*f.pool).kind()==Progress::Kind::waiting,"close passed paused finite initializer");
  proceed=true;f.settle(request);check(request.state==alloc::Preparation::State::cancelled,"cancelled constructor published");
  for(auto byte:alloc::bytes(neighbour))check(byte==std::byte{0x5a},"prefault/initializer changed shared neighbour");
  alloc::release(neighbour);alloc::release(unrelated);f.close_all();
  std::puts("{\"case\":\"finite-paused-shared-neighbour-cancel-close\",\"passed\":true}");
}

void finite_uncertain_cleanup_responsive_and_bounded(){
  Fixture f;auto neighbour=take(alloc::allocate(*f.pool,small_request(256)));std::memset(neighbour.address,0x5a,neighbour.size);
  const auto baseline=f.arena->charged_bytes.load();const auto mapped=alloc::statistics(*f.pool).mapped_bytes;
  alloc::Preparation request;SmallConstructor constructor;request.construct=SmallConstructor::run;request.context=&constructor;
  fail_protect=true;cleanup_failures=1000;
  done(alloc::prepare(*f.pool,request,small_request(3072),f.preparer.lane));f.settle(request);
  check(request.state==alloc::Preparation::State::repair_wait && request.backend && constructor.calls==0,
        "uncertain VM did not retain explicit cleanup debt or called constructor");
  const auto debt=f.arena->charged_bytes.load();const auto paused_calls=helper_protects.load();
  for(unsigned i=0;i<1000;++i){worker::poll(f.lane,32);std::this_thread::yield();}
  check(helper_protects==paused_calls && f.arena->charged_bytes==debt,"persistent cleanup failure busy-looped or refunded");
  auto healthy=take(alloc::allocate(*f.pool,small_request(256)));alloc::release(healthy);
  check(alloc::close(*f.pool).kind()==Progress::Kind::failed,"close hid physical cleanup debt");
  alloc::cancel(request);cleanup_failures=0;done(alloc::retry_cleanup(request));f.settle(request);
  check(request.state==alloc::Preparation::State::cancelled && !request.backend,"explicit retry did not retire debt");
  check(f.arena->charged_bytes==baseline && alloc::statistics(*f.pool).mapped_bytes==mapped,
        "cleanup did not refund exact uncertain page/record charge");
  for(auto byte:alloc::bytes(neighbour))check(byte==std::byte{0x5a},"uncertain cleanup changed live neighbour");
  alloc::release(neighbour);f.close_all();
  std::puts("{\"case\":\"finite-uncertain-cleanup-debt-explicit-retry-live-neighbour\",\"passed\":true}");
}

void finite_partial_cleanup_page_reused_before_original_settlement(){
  Fixture f;PreparerService independent;
  std::vector<alloc::Allocation> neighbours;
  neighbours.push_back(take(alloc::allocate(*f.pool,small_request(256,64))));
  auto* domain=alloc::detail::route(*neighbours.front().storage);
  check(domain->finite,"partial-cleanup fixture needs finite storage");
  auto& group=*static_cast<alloc::detail::Group*>(domain->context);
  auto& geometry=*group.geometry;
  const auto page=base_page(), slots=geometry.slots_per_grid();
  const auto bytes=geometry.capacity(geometry.class_count()-1);
  std::vector<alloc::detail::Home> reserved;
  alloc::detail::Home straddler,reusable;
  bool found=false;
  for(std::size_t grid=0;grid<geometry.identity_count()/slots && !found;++grid){
    const auto identity=take(geometry.claim_identity(~(std::uint64_t{1}<<grid)));
    for(;;){
      auto home=geometry.claim_home(identity,bytes,16);
      if(!home){check(home.error()==Error::capacity,"enumerating physical Homes failed");break;}
      reserved.push_back(*home);
    }
    for(const auto& target:reserved){
      const auto begin=reinterpret_cast<std::uintptr_t>(target.address);
      if(begin/page==(begin+target.capacity-1)/page)continue;
      for(const auto& candidate:reserved){
        const auto first=reinterpret_cast<std::uintptr_t>(candidate.address);
        if(first/page==begin/page && (first+candidate.capacity-1)/page==begin/page){
          straddler=target;reusable=candidate;found=true;break;
        }
      }
      if(found)break;
    }
    done(geometry.release_identity(identity));
    if(!found){for(const auto& home:reserved)done(geometry.release_home(home));reserved.clear();}
  }
  check(found && straddler.pages.count==2 && reusable.pages.count==1 &&
      reusable.pages.first==straddler.pages.first,"no native two-page cleanup/reuse witness");
  // Fill earlier identity grids with real live bytes. Reserve alternate Homes
  // without backing: filling their pages would hide partial failure on 64KiB.
  while(neighbours.size()<std::size_t(straddler.grid)*slots)
    neighbours.push_back(take(alloc::allocate(*f.pool,small_request(256,64))));
  for(auto& neighbour:neighbours){
    check(alloc::detail::route(*neighbour.storage)->context==&group,"identity cohort escaped its Group");
    std::memset(neighbour.address,0x5a,neighbour.size);
  }
  check(!geometry.mapped(straddler.pages.first) && !geometry.mapped(straddler.pages.first+1),
      "fixture backed an intended cold straddler page");
  done(geometry.release_home(straddler));
  const auto baseline=f.arena->charged_bytes.load();const auto baseline_metadata=alloc::statistics(*f.pool).metadata_bytes;
  alloc::Preparation original;fail_protect=true;cleanup_skip=1;cleanup_failures=1000;
  failed_protection_address=nullptr;failed_protection_bytes=0;
  done(alloc::prepare(*f.pool,original,small_request(bytes,16),f.preparer.lane));f.settle(original);
  const auto debt=alloc::statistics(*f.pool);const auto record=debt.metadata_bytes-baseline_metadata;
  check(failed_protection_address==geometry.page_address(straddler.pages.first) &&
      failed_protection_bytes==2*page,"helper did not exercise the planned native two-page operation");
  check(original.state==alloc::Preparation::State::repair_wait && debt.uncertain_bytes==page &&
        debt.preparing_bytes==0 && f.arena->charged_bytes==baseline+page+record,
        "partial cleanup retained/refunded wrong original page charge");
  check(!geometry.mapped(straddler.pages.first) && geometry.preparing(straddler.pages.first+1),
      "partial cleanup did not distinguish confirmed first discard from uncertain second page");
  done(geometry.release_home(reusable));
  alloc::Preparation other;SmallConstructor constructor;constructor.gated=true;
  other.construct=SmallConstructor::run;other.context=&constructor;arm_gate();
  done(alloc::prepare(*f.pool,other,small_request(bytes,16),independent.lane));wait_inside();
  check(constructor.address==reusable.address,"new Home did not reuse confirmed freed page");
  const auto both=alloc::statistics(*f.pool);
  check(both.preparing_bytes==page && both.uncertain_bytes==page,
        "new pending Home/old uncertain page custody conflated");
  cleanup_failures=0;done(alloc::retry_cleanup(original));f.settle(original);
  const auto resolved=alloc::statistics(*f.pool);
  check(original.state==alloc::Preparation::State::failed && original.error==Error::io &&
        other.state==alloc::Preparation::State::pending && resolved.preparing_bytes==page &&
        resolved.uncertain_bytes==0 && f.arena->charged_bytes==baseline+page+record,
        "original end cleared/refunded unrelated Home's reused page");
  for(const auto& neighbour:neighbours)
    for(auto byte:alloc::bytes(neighbour))check(byte==std::byte{0x5a},"partial discard changed original neighbour");
  proceed=true;f.settle(other);auto allocation=take(alloc::take(other));
  for(auto byte:alloc::bytes(allocation))check(byte==std::byte{0x42},"reused page constructor lost");
  alloc::release(allocation);
  for(const auto& home:reserved)
    if(home.address!=straddler.address && home.address!=reusable.address)done(geometry.release_home(home));
  for(auto& value:neighbours)alloc::release(value);independent.stop();
  std::printf("{\"case\":\"finite-partial-page-cleanup-reuse-before-original-settlement\",\"passed\":true,\"native_page\":%zu,\"straddler_grid\":%u,\"straddler_slot\":%u}\n",
      page,unsigned(straddler.grid),unsigned(straddler.slot));
}

void finite_constructor_failure_keeps_confirmed_pages(){
  Fixture f;alloc::Preparation failed;SmallConstructor constructor;constructor.fail=true;
  failed.construct=SmallConstructor::run;failed.context=&constructor;
  done(alloc::prepare(*f.pool,failed,small_request(),f.preparer.lane));f.settle(failed);
  check(failed.state==alloc::Preparation::State::failed && failed.error==Error::rejected && constructor.calls==1,
        "confirmed finite constructor failure result");
  const auto charge=f.arena->charged_bytes.load();const auto vm=helper_protects+helper_maps;
  alloc::Preparation next;done(alloc::prepare(*f.pool,next,small_request(),f.preparer.lane));f.settle(next);
  auto result=take(alloc::take(next));check(next.reused && f.arena->charged_bytes==charge && vm==helper_protects+helper_maps,
        "constructor failure replayed VM or stranded Home");alloc::release(result);
  std::puts("{\"case\":\"finite-confirmed-vm-constructor-failure-reuse\",\"passed\":true}");
}

void finite_aligned_home_grid_retry_tight_budget(){
  Fixture f(std::max(std::size_t{64*1024},base_page()+16*1024));
  std::vector<alloc::Allocation> values;
  alloc::Preparation first;done(alloc::prepare(*f.pool,first,small_request(320,128),f.preparer.lane));f.settle(first);
  values.push_back(take(alloc::take(first)));
  auto& group=*static_cast<alloc::detail::Group*>(alloc::detail::route(*values.front().storage)->context);
  const auto first_home=take(group.geometry->bound_home({values.front().storage->identity()}));
  const auto alignment=2*(first_home.capacity & -first_home.capacity);
  const auto start=reinterpret_cast<std::uintptr_t>(first_home.address)-first_home.slot*first_home.capacity;
  std::size_t legal=0;
  for(std::size_t slot=0;slot<group.geometry->slots_per_grid();++slot)
    legal+=(start+slot*first_home.capacity)%alignment==0;
  check(first_home.grid==0 && legal && legal<group.geometry->slots_per_grid(),"fixture lacks aligned Home pressure");
  for(std::size_t i=1;i<=legal;++i){
    alloc::Preparation request;done(alloc::prepare(*f.pool,request,small_request(320,alignment),f.preparer.lane));f.settle(request);
    values.push_back(take(alloc::take(request)));
    check(alloc::detail::route(*values.back().storage)->context==&group,"aligned retry created another finite Group");
    const auto home=take(group.geometry->bound_home({values.back().storage->identity()}));
    check(home.grid==(i<legal?0:1),"aligned Home pressure did not retry the next grid");
  }
  const auto statistics=alloc::statistics(*f.pool);
  check(statistics.metadata_bytes==group.metadata_charge,"aligned retry allocated extra metadata inventory");
  for(const auto& value:values)check(reinterpret_cast<std::uintptr_t>(value.address)%alignment==0,"aligned Home violated");
  for(auto& value:values)alloc::release(value);
  std::puts("{\"case\":\"finite-aligned-home-other-grid-tight-budget\",\"passed\":true}");
}

void finite_prepared_before_fresh_group(){
  Fixture f;std::array<alloc::Allocation,255> occupied;
  auto prepared=take(alloc::allocate(*f.pool,small_request(320)));auto* address=prepared.address;
  for(auto& value:occupied)value=take(alloc::allocate(*f.pool,small_request(256)));
  auto newgroup=take(alloc::allocate(*f.pool,small_request(256)));alloc::release(prepared);
  const auto before=alloc::statistics(*f.pool).mapped_bytes;alloc::Preparation request;
  done(alloc::prepare(*f.pool,request,small_request(320),f.preparer.lane));f.settle(request);auto result=take(alloc::take(request));
  check(result.address==address && request.reused && alloc::statistics(*f.pool).mapped_bytes==before,
        "fresh-head Group hid ready older candidate");
  alloc::release(result);alloc::release(newgroup);for(auto& value:occupied)alloc::release(value);
  std::puts("{\"case\":\"finite-prepared-before-fresh-advisory-order\",\"passed\":true}");
}

void finite_mixed_preferred_cadence(){
  for(bool directory_first:{false,true}){
    Fixture f;
    std::vector<alloc::Allocation> values;
    values.reserve(20*17);
    const auto ownervm=owner_maps+owner_protects;
    auto append=[&](std::size_t bytes){
      alloc::Preparation request;
      done(alloc::prepare(*f.pool,request,small_request(bytes),f.preparer.lane));
      f.settle(request);
      auto value=take(alloc::take(request));
      const auto expected=bytes==64 ? (base_page()==65536?256u:64u) : (base_page()>=16384?1088u:1280u);
      check(alloc::capacity(value)==expected && reinterpret_cast<std::uintptr_t>(value.address)%64==0,
          "mixed preparation inflated a preferred class or lost alignment");
      for(auto byte:alloc::bytes(value))check(byte==std::byte{0},"mixed preparation omitted initialization");
      std::memset(value.address,1+values.size()%251,value.size);
      values.push_back(std::move(value));
    };
    for(unsigned batch=0;batch<20;++batch){
      if(directory_first)append(1088);
      for(unsigned i=0;i<16;++i)append(64);
      if(!directory_first)append(1088);
    }
    // 320 small allocations exceed one Group's 256 identity slots. Keep the
    // complete mixed population live to expose class/page pressure on retry.
    for(std::size_t i=0;i<values.size();++i)
      for(auto byte:alloc::bytes(values[i]))
        check(byte==std::byte(1+i%251),"later mixed preparation overwrote live neighbours");
    const auto inventory=alloc::statistics(*f.pool);
    check(inventory.preparing_bytes==0 && inventory.uncertain_bytes==0 &&
          inventory.preparing_reserved_bytes==0 &&
          f.arena->charged_bytes==inventory.mapped_bytes+inventory.metadata_bytes,
        "settled mixed preparation lost exact physical/metadata charge");
    check(ownervm==owner_maps+owner_protects,"mixed preparation ran VM work on the owner");
    for(auto& value:values)alloc::release(value);
    f.close_all();
  }
  std::puts("{\"case\":\"finite-preferred-mixed-cadence-both-orders\",\"passed\":true}");
}

void finite_other_palette_ready_at_limit(){
  const auto history=[](std::size_t limit){
    Fixture f(limit);
    auto seed=take(alloc::allocate(*f.pool,small_request(256)));
    auto neighbour=take(alloc::allocate(*f.pool,small_request(256)));
    std::memset(neighbour.address,0x73,neighbour.size);
    auto* prior=seed.address;
    alloc::release(seed);
    const auto before=alloc::statistics(*f.pool);
    const auto baseline=f.arena->charged_bytes.load();
    const auto vm=helper_maps+helper_protects+owner_maps+owner_protects;
    alloc::Preparation request;
    done(alloc::prepare(*f.pool,request,small_request(),f.preparer.lane));
    // The accepted request owns real transient metadata until its owner reply.
    // The repeat budget permits exactly that peak, not a second Group/page.
    const auto peak=f.arena->charged_bytes.load();
    check(peak>baseline && peak<=limit,"pending ready preparation lost its charged record");
    f.settle(request);
    auto value=take(alloc::take(request));
    const auto after=alloc::statistics(*f.pool);
    check(request.reused && value.address==prior && alloc::capacity(value)==256 &&
          f.arena->charged_bytes==baseline && after.mapped_bytes==before.mapped_bytes &&
          after.metadata_bytes==before.metadata_bytes && after.reserved_bytes==before.reserved_bytes,
        "preferred policy stranded already charged other-palette backing");
    check(vm==helper_maps+helper_protects+owner_maps+owner_protects,
        "ready other-palette preparation performed new VM work");
    for(auto byte:alloc::bytes(value))check(byte==std::byte{0},"ready reuse omitted initialization");
    for(auto byte:alloc::bytes(neighbour))check(byte==std::byte{0x73},"ready reuse damaged live neighbour");
    alloc::release(value);alloc::release(neighbour);f.close_all();
    return peak;
  };
  const auto exact_peak=history(32*1024*1024);
  check(history(exact_peak)==exact_peak,"identical ready preparation changed its required peak");
  std::puts("{\"case\":\"finite-ready-other-palette-exact-pending-budget\",\"passed\":true}");
}

void finite_other_palette_unprepared_under_pressure(){
  const auto page=base_page();
  if(page==65536){
    std::puts("{\"case\":\"finite-unprepared-other-palette-pressure\",\"applicable\":false,\"reason\":\"native-B-only-profile\"}");
    return;
  }
  // Measure the real public charges, including the transient preparation record.
  // These observations size repeat budgets; they do not assert private layouts.
  const auto costs=[](){
    Fixture f;
    auto seed=take(alloc::allocate(*f.pool,small_request(64,16)));
    const auto baseline=f.arena->charged_bytes.load();
    alloc::Preparation request;
    done(alloc::prepare(*f.pool,request,small_request(64,16),f.preparer.lane));
    const auto extra=f.arena->charged_bytes.load()-baseline;
    f.settle(request);auto prepared=take(alloc::take(request));
    check(extra>0 && f.arena->charged_bytes==baseline,"ready calibration changed backing or lost request charge");
    alloc::release(prepared);alloc::release(seed);f.close_all();
    return std::array<std::size_t,2>{baseline,extra};
  }();
  const auto b_metadata=[](){
    Fixture f;
    auto seed=take(alloc::allocate(*f.pool,small_request(256)));
    const auto inventory=alloc::statistics(*f.pool);
    check(inventory.mapped_bytes==base_page() &&
          f.arena->charged_bytes==inventory.metadata_bytes+inventory.mapped_bytes,
        "fresh B calibration did not isolate metadata plus one page");
    alloc::release(seed);f.close_all();
    return std::size_t(inventory.metadata_bytes);
  }();
  for(bool asynchronous:{false,true}){
    for(const auto available:{page-1,page,std::max(page,b_metadata)}){
      const auto limit=costs[0]+(asynchronous?costs[1]:0)+available;
      Fixture f(limit);
      auto seed=take(alloc::allocate(*f.pool,small_request(64,16)));
      std::memset(seed.address,0x6d,seed.size);
      const auto before=alloc::statistics(*f.pool);
      check(f.arena->charged_bytes==costs[0],"identical A seed changed its physical charge");
      alloc::Allocation value;
      const bool fits=available>=page;
      if(asynchronous){
        alloc::Preparation request;
        const auto accepted=alloc::prepare(*f.pool,request,small_request(256),f.preparer.lane);
        if(fits){
          done(accepted);
          check(f.arena->charged_bytes==costs[0]+costs[1]+page,
              "fallback charged fresh metadata instead of one existing-Group page");
          f.settle(request);value=take(alloc::take(request));
        }else check(!accepted && accepted.error()==Error::capacity &&
                   request.state==alloc::Preparation::State::idle && !request.backend && !request.value,
            "one-page-short preparation admitted work or changed physical custody");
      }else{
        auto accepted=alloc::allocate(*f.pool,small_request(256));
        if(fits)value=take(std::move(accepted));
        else check(!accepted && accepted.error()==Error::capacity,
            "one-page-short direct allocation unexpectedly succeeded");
      }
      const auto after=alloc::statistics(*f.pool);
      check(after.metadata_bytes==before.metadata_bytes && after.reserved_bytes==before.reserved_bytes &&
            after.mapped_bytes==before.mapped_bytes+(fits?page:0) &&
            after.preparing_bytes==0 && after.uncertain_bytes==0 && after.preparing_reserved_bytes==0 &&
            f.arena->charged_bytes==costs[0]+(fits?page:0),
          "pressure fallback stranded metadata, refunded live bytes or lost exact backing charge");
      for(auto byte:alloc::bytes(seed))check(byte==std::byte{0x6d},"pressure fallback changed existing live bytes");
      if(fits){
        check(value.size==256 && alloc::capacity(value)==256 &&
              reinterpret_cast<std::uintptr_t>(value.address)%64==0,
            "fallback lost requested extent, capacity or alignment");
        for(auto byte:alloc::bytes(value))check(byte==std::byte{0},"fallback omitted body initialization");
      }
      alloc::release(value);alloc::release(seed);f.close_all();
    }
  }
  std::puts("{\"case\":\"finite-unprepared-other-palette-pressure\",\"passed\":true}");
}

}

extern "C" {
void* __real_mmap(void*,std::size_t,int,int,int,off_t);
int __real_mprotect(void*,std::size_t,int);
int __real_munmap(void*,std::size_t);

void* __wrap_mmap(void* a,std::size_t n,int p,int f,int fd,off_t o){
  if(owner_thread)++owner_maps;else ++helper_maps;
  if(!owner_thread && gate.exchange(false)){
    inside.store(true,std::memory_order_release);
    while(!proceed.load(std::memory_order_acquire))std::this_thread::yield();
  }
  if(!owner_thread && fail_map.exchange(false)){errno=ENOMEM;return MAP_FAILED;}
  auto* result=__real_mmap(a,n,p,f,fd,o);
  if(!owner_thread && fail_metadata && p==PROT_NONE && result!=MAP_FAILED)metadata_reservation_seen=true;
  return result;
}

void* __real__ZnwmRKSt9nothrow_t(std::size_t,const std::nothrow_t&) noexcept;
void* __wrap__ZnwmRKSt9nothrow_t(std::size_t bytes,const std::nothrow_t& tag) noexcept{
  if(!owner_thread && bytes==metadata_size && fail_metadata.exchange(false)){
    check(metadata_reservation_seen,"heap metadata allocation preceded real payload reservation");
    return nullptr;
  }
  return __real__ZnwmRKSt9nothrow_t(bytes,tag);
}

int __wrap_mprotect(void* a,std::size_t n,int p){
  if(owner_thread)++owner_protects;else ++helper_protects;
  if(!owner_thread && p==PROT_NONE && cleanup_failures.load()){
    if(cleanup_skip.load()) --cleanup_skip;
    else { --cleanup_failures;errno=EIO;return -1; }
  }
  if(!owner_thread && p!=(PROT_NONE) && fail_protect.exchange(false)){
    failed_protection_address=a;failed_protection_bytes=n;
    check(__real_mprotect(a,std::size_t(::sysconf(_SC_PAGESIZE)),p)==0,"partial mprotect setup");
    errno=EIO;return -1;
  }
  return __real_mprotect(a,n,p);
}

int __wrap_munmap(void* a,std::size_t n){
  if(!owner_thread && fail_unmap.exchange(false)){errno=EIO;return -1;}
  return __real_munmap(a,n);
}
}

int main(){
  paused_cancel_close(); ready_owner_semantics(); mapping_failure(); fresh_failure_moves_pending_window(); uncertain_failure_retry();
  bounded_charge_refusal();
  terminal_records_after_pool_replacement(); uninitialized_prefault();
  cancelled_constructor_and_same_fallback(); explicit_preparer_reclaim_cancel_close();
  explicit_preparer_real_format_and_owner_operations(); idle_reuse_exclusive_without_vm();
  reused_window_partial_growth_and_retry(); repeated_idle_prepare_cancel_cycles(); constructed_delivery_and_unread_ready();
  finite_fresh_bounded_start_cancel_close(); finite_control_cancel_reopens_palette();
  finite_control_failures_own_partial_backing();
  finite_delivered_owner_operations();
  finite_reuse_uninitialized_prefault(); finite_mapped_neighbour_cancel_paused_constructor();
  finite_uncertain_cleanup_responsive_and_bounded(); finite_partial_cleanup_page_reused_before_original_settlement();
  finite_constructor_failure_keeps_confirmed_pages();
  finite_aligned_home_grid_retry_tight_budget();finite_prepared_before_fresh_group();
  finite_mixed_preferred_cadence();finite_other_palette_ready_at_limit();
  finite_other_palette_unprepared_under_pressure();
  std::printf("{\"summary\":\"passed\",\"helper_mmap\":%u,\"owner_mmap\":%u,\"helper_mprotect\":%u,\"owner_mprotect\":%u,\"helper_real_ikea_constructions\":%u,\"finite_async_implemented\":true,\"preparation_frame_bytes\":%zu}\n",
    helper_maps.load(),owner_maps.load(),helper_protects.load(),owner_protects.load(),helper_constructions.load(),sizeof(alloc::Preparation));
}
