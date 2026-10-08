#include "inventory.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <new>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <thread>
#include <time.h>

namespace orbital_bench::packed {
namespace a = orbital::allocation;
namespace w = orbital::worker;
using State = a::Preparation::State;
using Clock = std::chrono::steady_clock;
namespace {
std::uint64_t cpu_ns(clockid_t clock) {
  timespec value{};
  if (::clock_gettime(clock, &value)) throw std::runtime_error("preparer CPU clock failed");
  return std::uint64_t(value.tv_sec) * 1000000000ull + value.tv_nsec;
}
std::uint64_t duration(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
bool terminal(const a::Preparation& p) {
  return p.state != State::pending && p.state != State::repair_wait && !p.backend && !p.preparer;
}
a::AllocationRequest shape() {
  return {.bytes=page_bytes, .reserved_bytes=page_bytes+64, .alignment=64,
      .readable_padding=64, .initialization=a::Initialization::uninitialized};
}
}
// The small placed service from Loom's qualified caller. Its immutable Lane
// hint wakes every actual outbound phase, including internal owner reposts.
void Inventory::Helper::start(int cpu) {
  lane.notify_posted=notify; lane.notify_context=this;
  owner_cpu = ::sched_getcpu();
  if (owner_cpu < 0) throw std::runtime_error("owner CPU unavailable");
  requested_cpu = cpu;
  if (requested_cpu == -1) {
    cpu_set_t allowed; CPU_ZERO(&allowed);
    if (::sched_getaffinity(0, sizeof(allowed), &allowed))
      throw std::runtime_error("owner affinity unavailable");
    requested_cpu = owner_cpu;
    for (int i=0; i<CPU_SETSIZE; ++i)
      if (CPU_ISSET(i,&allowed) && i!=owner_cpu) { requested_cpu=i; break; }
  }
  if (!w::spawn(thread, entry, this, requested_cpu))
    throw std::runtime_error("preparer spawn refused");
  unsigned status;
  while (!(status=startup.load(std::memory_order_acquire))) startup.wait(0,std::memory_order_acquire);
  if (status!=1 || observed_cpu!=requested_cpu) {
    if (!w::join(thread)) std::terminate();
    throw std::runtime_error("preparer placement refused");
  }
  if (::pthread_getcpuclockid(thread.native.native_handle(), &cpu_clock)) {
    stop();
    throw std::runtime_error("preparer CPU clock unavailable");
  }
}
void Inventory::Helper::entry(void* opaque) noexcept {
  auto& self=*static_cast<Helper*>(opaque);
  if (!w::adopt(self.lane,self.requested_cpu)) {
    self.startup.store(2,std::memory_order_release); self.startup.notify_one(); return;
  }
  self.observed_cpu=::sched_getcpu();
  self.startup.store(1,std::memory_order_release); self.startup.notify_one();
  for (;;) {
    const auto observed=self.epoch.load(std::memory_order_acquire);
    while (w::poll(self.lane,1)) {}
    if (self.stopping.load(std::memory_order_acquire)) {
      assert(!w::pending(self.lane)); break;
    }
    self.epoch.wait(observed,std::memory_order_acquire);
  }
  w::relinquish(self.lane);
  try { self.final_cpu=cpu_ns(CLOCK_THREAD_CPUTIME_ID); } catch (...) { std::terminate(); }
}
void Inventory::Helper::notify(void* context) noexcept {
  auto& self=*static_cast<Helper*>(context);
  self.epoch.fetch_add(1,std::memory_order_release); self.epoch.notify_one();
}
void Inventory::Helper::stop() {
  if (!thread.native.joinable()) return;
  stopping.store(true,std::memory_order_release);
  epoch.fetch_add(1,std::memory_order_release); epoch.notify_one();
  if (!w::join(thread)) throw std::runtime_error("preparer join failed");
  assert(!w::pending(lane));
}
std::uint64_t Inventory::Helper::cpu() const {
  return thread.native.joinable() ? cpu_ns(cpu_clock) : final_cpu;
}
Inventory::Helper::~Helper() { assert(!thread.native.joinable()); }

// A short critical section once per completed constructor and at cold interval
// boundaries gives a coherent total without a memory-model-sensitive seqlock.
void Inventory::Completed::success(const ConstructionDelta& d) noexcept {
  std::lock_guard lock(mutex);
  ++totals.completed_pages;
  totals.completed_pack_bytes+=d.issued_write_bytes;
  totals.completed_pack_spans+=d.issued_write_spans;
  totals.completed_index_bytes+=d.index_write_bytes;
  totals.completed_index_spans+=d.index_write_spans;
}
void Inventory::Completed::failure() noexcept {
  std::lock_guard lock(mutex); ++totals.constructor_failures;
}
PreparationStats Inventory::Completed::snapshot() const noexcept {
  std::lock_guard lock(mutex); return totals;
}
Inventory::Inventory(loom::Worker& owner,const Schema& schema,bool check_effects)
    :owner_(owner),schema_(schema),check_effects_(check_effects) {}
Inventory::~Inventory() { if (started_ && !stopped_) { try { drain(); } catch (...) { std::terminate(); } } }
orbital::Result<void> Inventory::construct(a::PreparedBytes input,void* opaque) noexcept {
  auto& slot=*static_cast<Slot*>(opaque);
  auto& self=*slot.inventory;
  assert(w::on_lane(self.helper_.lane) && !slot.page->allocation);
  try {
    slot.delta=construct_blank_page(*slot.page,self.schema_,input.body,input.readable_padding,self.check_effects_);
    slot.completed=true;
    self.completed_.success(slot.delta);
    return {};
  } catch (const std::bad_alloc&) {
    slot.page->bindings.reset(); self.completed_.failure();
    return std::unexpected(orbital::Error::capacity);
  } catch (...) {
    slot.page->bindings.reset(); self.completed_.failure();
    return std::unexpected(orbital::Error::invalid_argument);
  }
}
bool Inventory::offer(Slot& slot) {
  assert(accepting_ && terminal(slot.preparation) && !slot.preparation.value && !slot.page);
  // Reuse the stable record only after every queued borrower has returned.
  assert(!slot.preparation.changed.waiters);
  std::destroy_at(&slot.preparation); std::construct_at(&slot.preparation);
  slot.delta={}; slot.completed=false; slot.issued=false;
  try { slot.page=std::make_unique_for_overwrite<Page>(); }
  catch (const std::bad_alloc&) { ++stats_.admission_failures; return false; }
  slot.preparation.construct=construct; slot.preparation.context=&slot;
  if (!loom::prepare(owner_,slot.preparation,shape(),helper_.lane)) {
    slot.page.reset(); ++stats_.admission_failures; return false;
  }
  slot.issued=true; ++stats_.submissions; return true;
}
void Inventory::harvest(bool refill) {
  stats_.owner_messages+=loom::poll(owner_,{slot_count_+1,0});
  for (auto& slot:slots()) {
    auto& p=slot.preparation;
    if (p.state==State::repair_wait) {
      // Retained cleanup is still one inventory member. One explicit retry
      // per creation visit; final drain has a bounded failure budget below.
      ++stats_.cleanup_retries;
      if (!a::retry_cleanup(p)) throw std::runtime_error("preparation cleanup retry refused");
    }
    if (!terminal(p) || p.state==State::ready) continue;
    if (slot.issued) {
      if (slot.completed) {
        ++stats_.discarded_completed_pages;
        stats_.discarded_pack_bytes+=slot.delta.issued_write_bytes;
        stats_.discarded_pack_spans+=slot.delta.issued_write_spans;
        stats_.discarded_index_bytes+=slot.delta.index_write_bytes;
        stats_.discarded_index_spans+=slot.delta.index_write_spans;
      }
      slot.page.reset(); slot.issued=false;
    }
    if (refill && accepting_ && !slot.page) offer(slot);
  }
  observe_peak();
}
void Inventory::observe_peak() {
  std::uint64_t live{},ready{};
  for (const auto& slot:slots()) {
    const auto& p=slot.preparation;
    if (p.backend || p.state==State::ready) ++live;
    ready+=p.state==State::ready;
  }
  assert(live<=slot_count_);
  stats_.peak_inventory=std::max(stats_.peak_inventory,live);
  stats_.peak_ready=std::max(stats_.peak_ready,ready);
  stats_.peak_arena_charge=std::max(stats_.peak_arena_charge,owner_.arena->charged_bytes.load());
}
void Inventory::start(std::size_t count,int cpu,bool prefill) {
  assert(w::on_lane(owner_.lane));
  if (started_ || !count || count>4096) throw std::invalid_argument("invalid prepared inventory start");
  const auto start=Clock::now();
  const auto process_start=cpu_ns(CLOCK_PROCESS_CPUTIME_ID);
  slots_=std::make_unique<Slot[]>(count);
  slot_count_=count;
  for (auto& slot:slots()) slot.inventory=this;
  helper_.start(cpu); started_=true; accepting_=true;
  for (auto& slot:slots()) offer(slot);
  if (prefill) {
    const auto limit=Clock::now()+std::chrono::seconds(30);
    for (;;) {
      harvest(false);
      bool pending=false;
      for (const auto& slot:slots()) pending|=!terminal(slot.preparation);
      if (!pending) break;
      if (Clock::now()>limit) throw std::runtime_error("preparation startup settlement deadline");
      std::this_thread::yield();
    }
  }
  observe_peak();
  stats_.startup_ns=duration(start);
  stats_.startup_process_cpu_ns=cpu_ns(CLOCK_PROCESS_CPUTIME_ID)-process_start;
  stats_.startup_producer_cpu_ns=helper_.cpu();
}
std::unique_ptr<Page> Inventory::take_ready(PreparationDemands& request,ConstructionDelta& delta) {
  assert(started_ && accepting_ && w::on_lane(owner_.lane));
  ++request.demands; ++stats_.demands;
  harvest(true);
  for (auto& slot:slots()) {
    if (slot.preparation.state!=State::ready) continue;
    auto allocation=a::take(slot.preparation);
    if (!allocation) throw std::runtime_error("ready Page adoption refused");
    slot.page->allocation=std::move(*allocation);
    auto result=std::move(slot.page);
    // Callback-local delta is transferred for exact owner adoption once.
    delta=slot.delta;
    slot.issued=false;
    ++request.ready_hits; ++stats_.ready_hits;
    offer(slot); observe_peak(); return result;
  }
  ++request.stockouts; ++request.inline_fallbacks;
  ++stats_.stockouts; ++stats_.inline_fallbacks;
  return {};
}
void Inventory::drain() {
  assert(started_ && w::on_lane(owner_.lane));
  if (stopped_) return;
  accepting_=false;
  const auto start=Clock::now();
  stats_.unused_inventory=snapshot().inventory;
  for (auto& slot:slots()) a::cancel(slot.preparation);
  const auto limit=Clock::now()+std::chrono::seconds(30);
  const auto retries_before=stats_.cleanup_retries;
  for (;;) {
    harvest(false);
    bool pending=false;
    for (const auto& slot:slots()) pending|=!terminal(slot.preparation);
    if (!pending) break;
    if (stats_.cleanup_retries-retries_before>16 || Clock::now()>limit)
      throw std::runtime_error("physical preparation cleanup retained; helper must remain alive");
    std::this_thread::yield();
  }
  helper_.stop(); stopped_=true;
  stats_.drain_ns=duration(start);
  stats_.final_producer_cpu_ns=helper_.cpu();
  for (const auto& slot:slots()) assert(!slot.page && terminal(slot.preparation));
}
PreparationStats Inventory::snapshot(bool include_storage) const {
  assert(w::on_lane(owner_.lane));
  auto result=stats_, completed=completed_.snapshot();
  result.completed_pages=completed.completed_pages;
  result.completed_pack_bytes=completed.completed_pack_bytes;
  result.completed_pack_spans=completed.completed_pack_spans;
  result.completed_index_bytes=completed.completed_index_bytes;
  result.completed_index_spans=completed.completed_index_spans;
  result.constructor_failures=completed.constructor_failures;
  for (const auto& slot:slots()) {
    const auto& p=slot.preparation;
    result.ready+=p.state==State::ready;
    result.inflight+=p.backend!=nullptr;
    result.repair_wait+=p.state==State::repair_wait;
    result.inventory+=p.backend || p.state==State::ready;
    if (p.state==State::ready) result.ready_backing_bytes+=a::capacity(p.value);
  }
  result.producer_cpu_ns=started_ ? helper_.cpu() : 0;
  result.requested_cpu=helper_.requested_cpu; result.observed_cpu=helper_.observed_cpu;
  result.owner_cpu=helper_.owner_cpu;
  result.arena_charge=owner_.arena->charged_bytes.load();
  if (include_storage) {
    const auto pool=a::statistics(*owner_.memory);
    result.preparing_bytes=pool.preparing_bytes;
    result.preparing_reserved_bytes=pool.preparing_reserved_bytes;
    result.storage_observed=true;
  }
  result.inventory_limit=slot_count_;
  result.private_metadata_bound_bytes=sizeof(Inventory)+slot_count_*(sizeof(Slot)+sizeof(Page));
  return result;
}
} // namespace orbital_bench::packed
