#include "packed-construction.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace b=orbital_bench;
namespace p=b::packed;
namespace a=orbital::allocation;
using Clock=std::chrono::steady_clock;
namespace {
std::thread::id owner;
std::atomic<bool> block{}, entered{}, proceed{}, fail_constructor{}, fail_after_constructor{}, fail_owner_cpu_clock{};
orbital::Ref<a::Arena> retained_arena;
void check(bool valid,const char* why,std::source_location at=std::source_location::current()) {
  if (!valid) { std::fprintf(stderr,"%s:%u: %s\n",at.file_name(),at.line(),why); std::abort(); }
}
template<class Predicate> void await(Predicate predicate) {
  const auto limit=Clock::now()+std::chrono::seconds(10);
  while (!predicate()) { check(Clock::now()<limit,"progress deadline"); std::this_thread::yield(); }
}
std::vector<b::Row> input() {
  std::vector<b::Row> rows;
  for (std::size_t i=0;i<256;++i) rows.push_back({i*4,{i*4,std::uint32_t((i*17+3)&b::value_mask),std::uint32_t(i*11)}});
  return rows;
}
const b::Operation insertion{b::Kind::insert,1,{8,12345,0xfedcba98},1};
void verify(b::Store& store,std::vector<b::Row> expected) {
  expected.push_back({insertion.key,insertion.record});
  std::sort(expected.begin(),expected.end(),[](const auto& x,const auto& y){return x.key<y.key;});
  store.audit(); store.begin(false);
  std::array<b::Row,37> rows; std::size_t offset{}; b::Key next{};
  for (;;) {
    const auto count=store.scan(next,rows);
    if (!count) break;
    check(offset+count<=expected.size(),"scan produced extra rows");
    for (std::size_t i=0;i<count;++i) check(rows[i]==expected[offset+i],"full-state independent row oracle differs");
    offset+=count; next=rows[count-1].key+1;
  }
  check(offset==expected.size(),"scan lost rows");
  for (const auto& row:expected) check(store.get(row.key)==row.record,"point decode differs from independent input");
  check(!store.get(2),"gap became visible"); store.end();
}
void closed(std::unique_ptr<b::Store>& store) {
  store.reset();
  check(retained_arena && retained_arena->charged_bytes.load()==0,"physical teardown retained Arena charge");
  retained_arena={};
}
void same_effects(const b::Counters& after,const b::Counters& before,const b::Counters& wanted) {
  check(after.issued_write_bytes-before.issued_write_bytes==wanted.issued_write_bytes &&
        after.issued_write_spans-before.issued_write_spans==wanted.issued_write_spans &&
        after.index_write_bytes-before.index_write_bytes==wanted.index_write_bytes &&
        after.index_write_spans-before.index_write_spans==wanted.index_write_spans &&
        after.allocated_pages-before.allocated_pages==1 && after.splits-before.splits==1,
        "ready Page adoption changed or duplicated owner-accounted effects");
}
void ready_adoption() {
  const auto rows=input(); b::Counters wanted;
  {
    auto store=b::packed_store(64u<<20,b::Access::owner_ready,true); store->load(rows);
    const auto before=store->counters(false);
    check(b::execute(*store,insertion).changes==1,"control insertion refused");
    const auto after=store->counters(false);
    wanted.issued_write_bytes=after.issued_write_bytes-before.issued_write_bytes;
    wanted.issued_write_spans=after.issued_write_spans-before.issued_write_spans;
    wanted.index_write_bytes=after.index_write_bytes-before.index_write_bytes;
    wanted.index_write_spans=after.index_write_spans-before.index_write_spans;
    verify(*store,rows); closed(store);
  }
  auto store=b::packed_store(64u<<20,b::Access::owner_ready,true); store->load(rows);
  const auto before=store->counters(false);
  store->start_preparation(2,-1,true);
  auto stats=store->preparation_stats();
  check(stats.ready==2 && stats.inventory==2 && stats.completed_pages==2,"prefill did not establish explicit starting stock");
  check(stats.completed_pack_bytes==2*5760 && stats.completed_index_bytes==2*544,"completed construction oracle differs");
  check(b::execute(*store,insertion).changes==1,"prepared insertion refused");
  const auto flags=store->preparation_demands();
  check(flags.demands==1 && flags.ready_hits==1 && !flags.stockouts && !flags.inline_fallbacks,"ready take flags differ");
  same_effects(store->counters(false),before,wanted);
  store->drain(); stats=store->preparation_stats();
  check(!stats.inventory && !stats.inflight && !stats.repair_wait && stats.peak_inventory<=2,"bounded physical inventory did not drain");
  check(stats.completed_pages==stats.ready_hits+stats.discarded_completed_pages,"unadopted completed Page work was omitted");
  check(stats.completed_pack_bytes==stats.completed_pages*5760 && stats.completed_index_bytes==stats.completed_pages*544,
      "completed producer totals are not independent full-schema extents");
  verify(*store,rows); closed(store);
  std::puts("ready adoption, owner effects, bounded stock and zero-charge teardown passed");
}
void stockout_and_unread_completion() {
  const auto rows=input();
  block=true; entered=false; proceed=false;
  auto store=b::packed_store(64u<<20,b::Access::owner_ready,true); store->load(rows);
  store->start_preparation(1,-1,false);
  await([]{return entered.load(std::memory_order_acquire);});
  check(b::execute(*store,insertion).changes==1,"stockout insertion refused");
  auto flags=store->preparation_demands();
  check(flags.demands==1 && !flags.ready_hits && flags.stockouts==1 && flags.inline_fallbacks==1,
      "physical stockout did not record real inline fallback");
  proceed.store(true,std::memory_order_release); proceed.notify_one();
  await([&]{return store->preparation_stats().completed_pages==1;});
  const auto before=store->preparation_stats();
  check(before.inventory==1 && before.inflight==1 && before.ready==0,"completion-pending work escaped inventory bound");
  for (unsigned i=0;i<1000;++i) {
    store->begin(false); check(store->get(1)==insertion.record,"read after stockout differs"); store->end();
  }
  const auto after=store->preparation_stats();
  check(after.owner_messages==before.owner_messages && after.submissions==before.submissions &&
        after.ready==0 && after.inflight==1,"ordinary point reads serviced or refilled producer work");
  store->drain();
  const auto settled=store->preparation_stats();
  check(settled.unused_inventory==1 && settled.discarded_completed_pages==1 && settled.completed_pages==1 &&
        settled.ready_hits==0 && !settled.inventory && !settled.inflight,
        "cancelled completion-pending construction was adopted, omitted or left physically live");
  block=false; verify(*store,rows); closed(store);
  std::puts("forced stockout, unserviced point reads, completed cancellation and zero-charge teardown passed");
}
void startup_refusal_and_retry() {
  const auto rows=input();
  auto store=b::packed_store(64u<<20,b::Access::owner_ready,true); store->load(rows);
  for (auto config:std::array<std::pair<std::size_t,int>,2>{{{1,1024},{4097,-1}}}) {
    bool caught=false;
    try { store->start_preparation(config.first,config.second,true); }
    catch (const std::exception&) { caught=true; }
    check(caught && store->preparation_stats().inventory_limit==0,"failed startup published inventory into Store");
    store->begin(false); check(store->get(rows[0].key)==rows[0].record,"startup refusal damaged live Store"); store->end();
  }
  store->start_preparation(1,-1,true);
  check(b::execute(*store,insertion).changes==1 && store->preparation_demands().ready_hits==1,
      "same Store could not retry successful preparation after startup refusal");
  store->drain(); verify(*store,rows); closed(store);
  std::puts("startup refusals, same-Store retry and zero-charge teardown passed");
}
void failed_drain_and_explicit_retry() {
  const auto rows=input();
  auto store=b::packed_store(64u<<20,b::Access::owner_ready,true); store->load(rows);
  store->start_preparation(1,-1,true);
  fail_owner_cpu_clock=true;
  bool refused=false;
  try { store->drain(); } catch (const std::runtime_error&) { refused=true; }
  check(refused && !fail_owner_cpu_clock.load() && store->preparation_stats().inventory==1,
      "drain refusal did not preserve accepted physical stock");
  // Admission stays stopped. A later request must use ordinary inline creation,
  // even though the abandoned producer stock is ready; retry is cleanup only.
  check(b::execute(*store,insertion).changes==1 && store->preparation_demands().demands==0,
      "failed drain re-enabled producer demand/refill");
  store->drain();
  const auto settled=store->preparation_stats();
  check(!settled.inventory && !settled.inflight && settled.discarded_completed_pages==1 && !settled.ready_hits,
      "successful explicit drain retry skipped physical stock cleanup");
  verify(*store,rows); closed(store);
  std::puts("diagnostic refusal before cleanup, stopped admission, explicit drain retry and zero-charge teardown passed");
}
void failed_constructor(bool after=false) {
  const auto rows=input(); fail_constructor=true; fail_after_constructor=after;
  auto store=b::packed_store(64u<<20,b::Access::owner_ready,true); store->load(rows);
  store->start_preparation(1,-1,true);
  check(store->preparation_stats().constructor_failures==1 && !store->preparation_stats().completed_pages,
      "failed partial constructor became completed work");
  check(b::execute(*store,insertion).changes==1,"inline fallback after failed constructor refused");
  const auto flags=store->preparation_demands();
  check(flags.stockouts==1 && flags.inline_fallbacks==1 && !flags.ready_hits,"failed constructor was adopted");
  store->drain(); fail_constructor=false;
  verify(*store,rows); closed(store);
  std::puts(after ? "completed private metadata refusal, real fallback and zero-charge teardown passed" : "partial constructor failure, real fallback and zero-charge teardown passed");
}
}
p::ConstructionDelta real_construct(p::Page&,const p::Schema&,std::span<std::byte>,std::size_t,bool)
  asm("__real__ZN13orbital_bench6packed20construct_blank_pageERNS0_4PageERKNS0_6SchemaESt4spanISt4byteLm18446744073709551615EEmb");
p::ConstructionDelta wrapped_construct(p::Page&,const p::Schema&,std::span<std::byte>,std::size_t,bool)
  asm("__wrap__ZN13orbital_bench6packed20construct_blank_pageERNS0_4PageERKNS0_6SchemaESt4spanISt4byteLm18446744073709551615EEmb");
p::ConstructionDelta wrapped_construct(p::Page& page,const p::Schema& schema,std::span<std::byte> body,std::size_t padding,bool effects) {
  if (std::this_thread::get_id()!=owner) {
    if (block.load(std::memory_order_acquire)) {
      entered.store(true,std::memory_order_release);
      while (!proceed.load(std::memory_order_acquire)) proceed.wait(false,std::memory_order_acquire);
    }
    if (fail_constructor.load(std::memory_order_acquire)) {
      if (fail_after_constructor.load(std::memory_order_acquire)) {
        (void)real_construct(page,schema,body,padding,effects);
        check(bool(page.bindings),"post-construction failure missed live private metadata");
      } else std::fill_n(body.data(),64,std::byte{0xa5});
      throw std::runtime_error("injected private constructor failure");
    }
  }
  return real_construct(page,schema,body,padding,effects);
}
orbital::Result<orbital::Ref<a::Arena>> real_arena(std::uint64_t,int)
  asm("__real__ZN7orbital10allocation10open_arenaEmi");
orbital::Result<orbital::Ref<a::Arena>> wrapped_arena(std::uint64_t,int)
  asm("__wrap__ZN7orbital10allocation10open_arenaEmi");
orbital::Result<orbital::Ref<a::Arena>> wrapped_arena(std::uint64_t bytes,int cpu) {
  check(!retained_arena,"overlapping test Arena");
  auto result=real_arena(bytes,cpu);
  if (result) retained_arena=*result;
  return result;
}
extern "C" int __real_clock_gettime(clockid_t,timespec*);
extern "C" int __wrap_clock_gettime(clockid_t clock,timespec* value) {
  if (std::this_thread::get_id()==owner && clock!=CLOCK_MONOTONIC &&
      fail_owner_cpu_clock.exchange(false,std::memory_order_relaxed)) {
    errno=EIO; return -1;
  }
  return __real_clock_gettime(clock,value);
}
int main() {
  owner=std::this_thread::get_id();
  ready_adoption(); stockout_and_unread_completion(); failed_constructor(); failed_constructor(true); startup_refusal_and_retry(); failed_drain_and_explicit_retry();
}
