#include <orbital/preparation.h>
#include "finite.h"
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
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

alloc::AllocationRequest page_request() { return {6304,65536,64,64,alloc::Initialization::zero}; }

void arm_gate() { proceed=false; inside=false; gate=true; }

void paused_cancel_close() {
  Fixture f; alloc::Preparation request;
  const auto maps=owner_maps.load(), protects=owner_protects.load();
  arm_gate(); done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); wait_inside();
  const auto charge=f.arena->charged_bytes.load();
  check(charge>8192,"private Window not fully charged before VM");
  check(!alloc::take(request),"unprepared backing consumable");
  auto stats=alloc::statistics(*f.pool);
  check(stats.mapped_bytes==0 && stats.reserved_bytes==0 && stats.preparing_bytes==8192 &&
        stats.preparing_reserved_bytes==65536 && stats.idle_bytes==0,
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
    const auto grown=alloc::grow(value,{7000,65536,64,64});
    check(!grown && grown.error()==Error::conflict,"foreign grow accepted prepared owner backing");
    const auto held=alloc::hold(value,{0,8});
    check(!held && held.error()==Error::conflict,"foreign hold accepted prepared owner backing");
  }); foreign.join();
  done(alloc::grow(value,{7000,65536,64,64}));
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
  check(f.arena->charged_bytes.load()==base_charge+8192+alloc::statistics(*f.pool).metadata_bytes/3,
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
  check(charge>8192 && uncertain.mapped_bytes==0 && uncertain.uncertain_bytes==8192 &&
        uncertain.reserved_bytes==65536 && charge==uncertain.metadata_bytes+uncertain.uncertain_bytes,
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
  std::array<unsigned char,2> resident{};
  check(::mincore(value.address,8192,resident.data())==0,"mincore prepared pages");
  check((resident[0]&1) && (resident[1]&1),"uninitialized request left foreground first-touch faults");
  check(owner_protects.load()==protects,"uninitialized preparation used owner VM");
  alloc::release(value);
  std::puts("{\"case\":\"uninitialized-no-padding-prefault\",\"passed\":true}");
}

void bounded_charge_refusal() {
  Fixture f(9000); alloc::Preparation first,second;
  arm_gate(); done(alloc::prepare(*f.pool,first,page_request(),f.preparer.lane)); wait_inside();
  const auto before=f.arena->charged_bytes.load();
  const auto refused=alloc::prepare(*f.pool,second,page_request(),f.preparer.lane);
  check(!refused && refused.error()==Error::capacity && second.state==alloc::Preparation::State::idle,
      "refusal accepted or mutated caller frame");
  check(f.arena->charged_bytes.load()==before,"refusal changed charge");
  alloc::cancel(first); proceed=true; f.settle(first);
  std::puts("{\"case\":\"bounded-inflight-charge-refusal\",\"passed\":true}");
}

using Format=sp::preset_format<20,sp::preset::compact>;
constexpr std::size_t rows=256, series_bytes=640, tuple_offset=640, tuple_stride=20;

struct Constructor {
  tp::layout layout;
  tp::constructor tuple;
  std::uint64_t written_bytes{};
  std::size_t issued_spans{}, completions{};
  Constructor() : layout([]{
    std::array<tp::code,tuple_stride> codes;
    for(std::size_t i=0;i<codes.size();++i)codes[i]={tp::byte(i),0,8};
    return take(tp::layout::make(tuple_stride,codes));
  }()),tuple(take(tp::constructor::make(layout))){}
  static Result<void> initialize(alloc::PreparedBytes destination,void* context)noexcept{
    const auto storage=destination.body;
    check(destination.readable_padding==64,"constructor did not receive readable padding contract");
    for(std::size_t i=0;i<destination.readable_padding;++i)
      check(storage.data()[storage.size()+i]==std::byte{0},"constructor padding not initialized");
    auto& own=*static_cast<Constructor*>(context);
    if(!owner_thread)++helper_constructions;
    auto series=take(sp::view<Format,std::uint8_t>::attach(rows,{{{
      {reinterpret_cast<std::uint8_t*>(storage.data()),series_bytes},Format::tile_bytes},{},{}}}));
    auto prepared=take(sp::bind_mutation(series));
    auto write=prepared.erase<std::uint32_t,sp::sum_change>();
    auto tuples=take(tp::view::bind(own.layout,
        {reinterpret_cast<tp::byte*>(storage.data()+tuple_offset),rows*tuple_stride},rows,tuple_stride));
    auto construct=take(tp::bind_constructor(own.tuple,tuples));
    std::array<std::uint32_t,rows> values{};
    for(std::size_t i=0;i<rows;++i)values[i]=std::uint32_t(11*i+5);
    std::array<tp::construction_input,rows> records{};
    for(std::size_t i=0;i<rows;++i){ records[i][0]=tp::byte(i); records[i][8]=tp::byte(255-i); }
    std::array<ikea::owner_write,rows*4> effects_storage;
    ikea::source_write_journal effects{effects_storage};
    check(bool(write.initialize(values,effects)),"SeriesPack initialize");
    const auto capture=[&]{
      for(const auto& effect:effects.entries()){
        const auto limit=effect.source==&series ? series_bytes :
                         effect.source==&tuples ? rows*tuple_stride : 0;
        check(limit && !effect.bytes.plane && effect.bytes.offset<=limit && effect.bytes.size<=limit-effect.bytes.offset,
            "constructor effect not source-qualified");
        own.written_bytes+=effect.bytes.size; ++own.issued_spans;
      }
      effects.used=0;
    };
    capture(); check(bool(construct.initialize(0,records,effects)),"TuplePack initialize"); capture();
    return {};
  }
  void verify(MutableBytes storage){
    auto series=take(sp::view<Format,std::uint8_t>::attach(rows,{{{
      {reinterpret_cast<std::uint8_t*>(storage.data()),series_bytes},Format::tile_bytes},{},{}}}));
    auto decoder=sp::bind_decoder<std::uint32_t>(series);
    auto tuples=take(tp::view::bind(layout,
        {reinterpret_cast<tp::byte*>(storage.data()+tuple_offset),rows*tuple_stride},rows,tuple_stride));
    const std::array<tp::byte,8> map{0,1,2,3,4,5,6,7};
    auto reader=take(tp::reader<8>::make(layout,map));
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
  construct.verify(alloc::bytes(ready)); done(alloc::grow(ready,{7000,65536,64,64}));
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
  Fixture f(9000); alloc::Preparation request; GatedConstructor builder;
  auto old=take(alloc::allocate(*f.pool,page_request())); auto* address=old.address;
  alloc::release(old); const auto charge=f.arena->charged_bytes.load();
  const auto vm_calls=owner_maps+helper_maps+owner_protects+helper_protects;
  request.construct=GatedConstructor::initialize; request.context=&builder;
  arm_gate(); done(alloc::prepare(*f.pool,request,page_request(),f.preparer.lane)); wait_inside();
  const auto stats=alloc::statistics(*f.pool);
  check(request.reused && stats.mapped_bytes==8192 && stats.preparing_bytes==0 &&
      stats.idle_bytes==0 && stats.reserved_bytes==65536 && f.arena->charged_bytes==charge,
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
  auto larger=page_request(); larger.bytes=20000;
  const auto maps=owner_maps+helper_maps;
  fail_protect=true; done(alloc::prepare(*f.pool,failed,larger,f.preparer.lane)); f.settle(failed);
  const auto uncertainty=alloc::statistics(*f.pool);
  const auto charge=f.arena->charged_bytes.load();
  check(failed.reused && failed.state==alloc::Preparation::State::failed && failed.error==Error::io,
      "reused partial preparation failure lost");
  check(uncertainty.mapped_bytes==8192 && uncertainty.uncertain_bytes==12288 &&
      uncertainty.preparing_bytes==0 && charge==uncertainty.mapped_bytes+uncertainty.uncertain_bytes+uncertainty.metadata_bytes,
      "reused known prefix/uncertain suffix charge was lost or double counted");
  for(std::size_t i=0;i<6304;++i)check(address[i]==std::byte{0x5a},"failed suffix protection corrupted prepared prefix");
  done(alloc::prepare(*f.pool,retry,larger,f.preparer.lane)); f.settle(retry);
  auto ready=take(alloc::take(retry));
  check(retry.reused && ready.address==address && f.arena->charged_bytes==charge,"retry lost identity or charged twice");
  check(maps==owner_maps+helper_maps,"reused protection retry remapped reservation");
  const auto confirmed=alloc::statistics(*f.pool);
  check(confirmed.mapped_bytes==20480 && confirmed.uncertain_bytes==0,"retry did not resolve conservative suffix charge");
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
  const auto planned=take(alloc::detail::FiniteGroup::create({alloc::detail::Palette::b,256}));
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

void finite_control_failures_own_partial_backing(){
  for(std::size_t alignment:{16,64}){
  for(bool partial:{false,true}){
    Fixture f;alloc::Preparation failed;
    const auto references=f.arena->references.load();
    const auto expected=partial?Error::capacity:Error::io;
    // Metadata heap ENOMEM happens after the real sparse payload reservation.
    auto planned=take(alloc::detail::FiniteGroup::create({alignment>=64?alloc::detail::Palette::b:alloc::detail::Palette::a,256}));
    metadata_size=planned->control_bytes();metadata_reservation_seen=false;
    if(partial)fail_metadata=true;else fail_map=true;
    done(alloc::prepare(*f.pool,failed,small_request(64,alignment),f.preparer.lane));f.settle(failed);
    const auto inventory=alloc::statistics(*f.pool);const auto charged=f.arena->charged_bytes.load();
    check(failed.state==alloc::Preparation::State::failed && failed.error==expected && !failed.backend &&
          !failed.owner && inventory.mapped_bytes==0 && inventory.preparing_bytes==0 &&
          inventory.uncertain_bytes==0 && inventory.allocations==0 && !failed.value &&
          inventory.metadata_bytes==charged && charged>0,
          "failed finite control lost terminal status or partial mapping custody");
    check((inventory.reserved_bytes>0)==partial,"failed finite control reported nonexistent/lost sparse mapping");
    check(!fail_metadata && (metadata_reservation_seen.load()==partial),"heap failure did not follow real payload reservation");
    check(f.arena->references.load()==references,"failed helper retained its preparation pin");
    auto healthy=take(alloc::allocate(*f.pool,small_request(64,alignment)));alloc::release(healthy);
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
  done(alloc::grow(allocation,{96,96,64,0}));
  std::thread foreign([&]{check(!alloc::hold(allocation,{0,1}),"foreign finite hold accepted");});foreign.join();
  alloc::release(held);alloc::release(allocation);
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
  Fixture f;auto neighbour=take(alloc::allocate(*f.pool,small_request()));std::memset(neighbour.address,0x5a,64);
  const auto baseline=f.arena->charged_bytes.load();const auto mapped=alloc::statistics(*f.pool).mapped_bytes;
  alloc::Preparation request;SmallConstructor constructor;request.construct=SmallConstructor::run;request.context=&constructor;
  fail_protect=true;cleanup_failures=1000;
  done(alloc::prepare(*f.pool,request,small_request(3072),f.preparer.lane));f.settle(request);
  check(request.state==alloc::Preparation::State::repair_wait && request.backend && constructor.calls==0,
        "uncertain VM did not retain explicit cleanup debt or called constructor");
  const auto debt=f.arena->charged_bytes.load();const auto paused_calls=helper_protects.load();
  for(unsigned i=0;i<1000;++i){worker::poll(f.lane,32);std::this_thread::yield();}
  check(helper_protects==paused_calls && f.arena->charged_bytes==debt,"persistent cleanup failure busy-looped or refunded");
  auto healthy=take(alloc::allocate(*f.pool,small_request()));alloc::release(healthy);
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
  // Occupy all one-page alignment2048 Homes in the first grid. The next
  // aligned candidate crosses two wholly absent pages, rather than choosing
  // a cheaper one-page alternative (which ordinary policy correctly prefers).
  std::array<alloc::Allocation,4> neighbours;
  for(auto& value:neighbours){value=take(alloc::allocate(*f.pool,small_request(3072,2048)));std::memset(value.address,0x5a,3072);}
  auto& neighbour=neighbours[0];
  const auto baseline=f.arena->charged_bytes.load();const auto baseline_metadata=alloc::statistics(*f.pool).metadata_bytes;
  alloc::Preparation original;fail_protect=true;cleanup_skip=1;cleanup_failures=1000;
  done(alloc::prepare(*f.pool,original,small_request(3072,2048),f.preparer.lane));f.settle(original);
  const auto debt=alloc::statistics(*f.pool);const auto record=debt.metadata_bytes-baseline_metadata;
  check(original.state==alloc::Preparation::State::repair_wait && debt.uncertain_bytes==4096 &&
        debt.preparing_bytes==0 && f.arena->charged_bytes==baseline+4096+record,
        "partial cleanup retained/refunded wrong original page charge");
  alloc::Preparation other;SmallConstructor constructor;constructor.gated=true;
  other.construct=SmallConstructor::run;other.context=&constructor;arm_gate();
  done(alloc::prepare(*f.pool,other,small_request(3072),independent.lane));wait_inside();
  check(constructor.address==neighbour.address+3072,"new Home did not reuse confirmed freed page");
  const auto both=alloc::statistics(*f.pool);
  check(both.preparing_bytes==4096 && both.uncertain_bytes==4096,
        "new pending Home/old uncertain page custody conflated");
  cleanup_failures=0;done(alloc::retry_cleanup(original));f.settle(original);
  const auto resolved=alloc::statistics(*f.pool);
  check(original.state==alloc::Preparation::State::failed && original.error==Error::io &&
        other.state==alloc::Preparation::State::pending && resolved.preparing_bytes==4096 &&
        resolved.uncertain_bytes==0 && f.arena->charged_bytes==baseline+4096+record,
        "original end cleared/refunded unrelated Home's reused page");
  for(auto byte:alloc::bytes(neighbour))check(byte==std::byte{0x5a},"partial discard changed original neighbour");
  proceed=true;f.settle(other);auto allocation=take(alloc::take(other));
  for(auto byte:alloc::bytes(allocation))check(byte==std::byte{0x42},"reused page constructor lost");
  alloc::release(allocation);for(auto& value:neighbours)alloc::release(value);independent.stop();
  std::puts("{\"case\":\"finite-partial-page-cleanup-reuse-before-original-settlement\",\"passed\":true}");
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

void finite_ninth_aligned_home_tight_budget(){
  Fixture f(64*1024);std::array<alloc::Allocation,9> values;
  for(auto& value:values){alloc::Preparation request;done(alloc::prepare(*f.pool,request,small_request(320,128),f.preparer.lane));f.settle(request);value=take(alloc::take(request));}
  const auto statistics=alloc::statistics(*f.pool);
  check(statistics.metadata_bytes<48*1024,"ninth aligned Home created second finite Group");
  for(const auto& value:values)check(reinterpret_cast<std::uintptr_t>(value.address)%128==0,"aligned Home violated");
  for(auto& value:values)alloc::release(value);
  std::puts("{\"case\":\"finite-ninth-aligned-home-other-grid-tight-budget\",\"passed\":true}");
}

void finite_prepared_before_fresh_group(){
  Fixture f;std::array<alloc::Allocation,255> occupied;
  auto prepared=take(alloc::allocate(*f.pool,small_request(320)));auto* address=prepared.address;
  for(auto& value:occupied)value=take(alloc::allocate(*f.pool,small_request()));
  auto newgroup=take(alloc::allocate(*f.pool,small_request()));alloc::release(prepared);
  const auto before=alloc::statistics(*f.pool).mapped_bytes;alloc::Preparation request;
  done(alloc::prepare(*f.pool,request,small_request(320),f.preparer.lane));f.settle(request);auto result=take(alloc::take(request));
  check(result.address==address && request.reused && alloc::statistics(*f.pool).mapped_bytes==before,
        "fresh-head Group hid ready older candidate");
  alloc::release(result);alloc::release(newgroup);for(auto& value:occupied)alloc::release(value);
  std::puts("{\"case\":\"finite-prepared-before-fresh-advisory-order\",\"passed\":true}");
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
  finite_fresh_bounded_start_cancel_close(); finite_control_failures_own_partial_backing();
  finite_delivered_owner_operations();
  finite_reuse_uninitialized_prefault(); finite_mapped_neighbour_cancel_paused_constructor();
  finite_uncertain_cleanup_responsive_and_bounded(); finite_partial_cleanup_page_reused_before_original_settlement();
  finite_constructor_failure_keeps_confirmed_pages();
  finite_ninth_aligned_home_tight_budget();finite_prepared_before_fresh_group();
  std::printf("{\"summary\":\"passed\",\"helper_mmap\":%u,\"owner_mmap\":%u,\"helper_mprotect\":%u,\"owner_mprotect\":%u,\"helper_real_ikea_constructions\":%u,\"finite_async_implemented\":true,\"preparation_frame_bytes\":%zu}\n",
    helper_maps.load(),owner_maps.load(),helper_protects.load(),owner_protects.load(),helper_constructions.load(),sizeof(alloc::Preparation));
}
