// Shared budget, demand notifications and the Arena reclamation worker.

#include "internal.h"
#include <algorithm>
#include <new>
#include <sched.h>
#include <system_error>
#include <utility>

namespace orbital::allocation::detail {

void ArenaState::reclaim(void* context) noexcept {
  auto& state = *static_cast<ArenaState*>(context);
  for (;;) {
    Job* job;
    {
      std::unique_lock lock(state.jobs_mutex);
      state.jobs_ready.wait(lock, [&] { return state.stopping || state.first; });
      if (!state.first) return;
      job = state.first;
      state.first = job->next;
      if (!state.first) state.last = nullptr;
      job->next = nullptr;
    }
    job->run(*job);
  }
}

ArenaState::~ArenaState() {
  if (!reclaimer.native.joinable()) return;
  { std::lock_guard lock(jobs_mutex); stopping = true; }
  jobs_ready.notify_one();
  const auto joined = worker::join(reclaimer);
  assert(joined);
  (void)joined;
}

void queue_notice(Pool& pool) noexcept;
// All links are protected by the Arena registry. The atomic head is also the
// idle refund shortcut; publishers never dereference a head read without lock.
bool interested(const Pool& pool) noexcept {
  return pool.internal->next_interested || pool.internal->previous_interested ||
      pool.arena->state->interested_pools.load(std::memory_order_relaxed) == &pool;
}

void remove_interest(Pool& pool) noexcept {
  if (!interested(pool)) return;
  auto& links = *pool.internal;
  if (links.previous_interested)
    links.previous_interested->internal->next_interested = links.next_interested;
  else pool.arena->state->interested_pools.store(links.next_interested, std::memory_order_seq_cst);
  if (links.next_interested)
    links.next_interested->internal->previous_interested = links.previous_interested;
  links.next_interested = nullptr;
  links.previous_interested = nullptr;
}

bool add_interest(Pool& pool) noexcept {
  if (interested(pool)) return false;
  auto* head = pool.arena->state->interested_pools.load(std::memory_order_relaxed);
  pool.internal->next_interested = head;
  if (head) head->internal->previous_interested = &pool;
  pool.arena->state->interested_pools.store(&pool, std::memory_order_seq_cst);
  return true;
}

[[gnu::noinline]] bool refused_claim(Pool& pool, std::uint64_t amount) noexcept {
  auto& arena = *pool.arena;
  if (amount > arena.byte_limit) return false; // A refund cannot make this fit.
  std::unique_lock lock(arena.state->registry, std::try_to_lock);
  if (!lock) {
    // Refusal never blocks the owner behind a preempted registry holder.
    // This retained owner Message permits another attempt after enrollment
    // was unavailable, even if a refund already took the empty-head shortcut.
    queue_notice(pool);
    return false;
  }
  const auto inserted = add_interest(pool);
  // SC head publication then charged recheck pairs with SC refund then head
  // observation: either this attempt sees the refund or its Pool is notified.
  // Acquire/release alone would permit both sides to miss the other publication.
  if (!claim_bytes<true>(arena, amount)) return false;
  if (inserted) remove_interest(pool); // This call never returned a refusal.
  return true;
}

void request_broadcast(Pool& pool) noexcept;

void refund(Pool& pool, std::uint64_t amount) noexcept {
  assert(worker::on_lane(*pool.lane));
  const auto old = pool.arena->charged_bytes.fetch_sub(amount, std::memory_order_seq_cst);
  assert(old >= amount);
  if (amount && pool.arena->state->interested_pools.load(std::memory_order_seq_cst))
    request_broadcast(pool);
}

void schedule(Arena& arena, Job& job) noexcept {
  auto& state = *arena.state;
  { std::lock_guard lock(state.jobs_mutex);
    if (state.last) state.last->next = &job;
    else state.first = &job;
    state.last = &job;
  }
  state.jobs_ready.notify_one();
}

bool start_broadcast(Pool& pool) noexcept {
  auto& state = *pool.arena->state;
  std::unique_lock lock(state.jobs_mutex, std::try_to_lock);
  if (!lock) return false;
  pool.internal->broadcast_requested = false;
  auto& job = pool.internal->broadcast_job;
  if (state.last) state.last->next = &job;
  else state.first = &job;
  state.last = &job;
  lock.unlock();
  state.jobs_ready.notify_one();
  return true;
}

void notice_handle(worker::Message& message, worker::Lane&) noexcept {
  auto& pool = *static_cast<Pool*>(message.context);
  pulse(pool.capacity_changed);
}

void notice_release(worker::Message& message) noexcept {
  auto& pool = *static_cast<Pool*>(message.context);
  auto& internal = *pool.internal;
  if (internal.broadcast_requested) {
    if (!start_broadcast(pool)) worker::post(*pool.lane, message);
    return;
  }
  // Move the old pin before permitting another publisher to fill this field.
  // Cold registry work and queued notices share this one retained lifecycle.
  auto retained = std::move(internal.notice_lifetime);
  internal.notice_pending.store(false, std::memory_order_release);
  // A refund can coalesce after handle's first pulse but before this release.
  // Pulse again after clearing pending so that refund cannot lose its wake.
  pulse(pool.capacity_changed);
}

const worker::MessageOps notice_ops{no_retain, notice_handle, notice_release};

bool retain_notice(Pool& pool) noexcept {
  bool idle = false;
  if (!pool.internal->notice_pending.compare_exchange_strong(idle, true,
      std::memory_order_acq_rel, std::memory_order_acquire)) return false;
  pool.internal->notice_lifetime = Ref<Arena>::retain(*pool.arena);
  return true;
}

void queue_notice(Pool& pool) noexcept {
  if (!retain_notice(pool)) return;
  worker::post(*pool.lane, pool.internal->notice);
}

void request_broadcast(Pool& pool) noexcept {
  pool.internal->broadcast_requested = true;
  if (!retain_notice(pool)) return;
  // A foreground rollback can notify other Pools without waiting for another
  // owner poll. Busy setup falls back to its already retained retry Message.
  if (!start_broadcast(pool)) worker::post(*pool.lane, pool.internal->notice);
}

void broadcast_run(Job& job) noexcept {
  auto& pool = *static_cast<Pool*>(job.context);
  auto& arena = *pool.arena;
  {
    std::lock_guard lock(arena.state->registry);
    // Refusal interest is one-shot. Unlink before posting so a later
    // owner refusal can enroll again; abandoned demand cannot keep waking.
    while (auto* pending = arena.state->interested_pools.load(std::memory_order_relaxed)) {
      remove_interest(*pending);
      queue_notice(*pending);
    }
  }
  worker::post(*pool.lane, pool.internal->notice);
}

} // namespace orbital::allocation::detail

namespace orbital::allocation {
using namespace detail;

Arena::Arena() = default;
Arena::~Arena() = default;
Pool::Pool() = default;
Pool::~Pool() = default;

Result<Ref<Arena>> open_arena(std::uint64_t byte_limit, int reclaimer_cpu) {
  if (!byte_limit || reclaimer_cpu < -1 || reclaimer_cpu >= CPU_SETSIZE)
    return std::unexpected(Error::invalid_argument);
  try {
    auto arena = std::make_unique<Arena>();
    arena->byte_limit = byte_limit;
    arena->state = std::make_unique<ArenaState>();
    auto started = worker::spawn(arena->state->reclaimer, ArenaState::reclaim,
        arena->state.get(), reclaimer_cpu);
    if (!started) return std::unexpected(started.error());
    auto result = Ref<Arena>::retain(*arena);
    arena.release();
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    catch (const std::system_error&) { return std::unexpected(Error::unavailable); }
}

Result<Pool*> add_pool(Arena& arena, worker::Lane& lane) {
  if (!worker::on_lane(lane)) return std::unexpected(Error::conflict);
  try {
    auto pool = std::make_unique<Pool>();
    pool->arena = &arena; pool->lane = &lane;
    pool->capacity_changed.lane = &lane;
    pool->internal = std::make_unique<PoolState>();
    pool->internal->notice.context = pool.get();
    pool->internal->notice.ops = &notice_ops;
    pool->internal->broadcast_job.context = pool.get();
    pool->internal->broadcast_job.run = broadcast_run;
    auto* result = pool.get();
    std::lock_guard lock(arena.state->registry);
    arena.state->pools.push_back(std::move(pool));
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

Result<void> remove_pool(Pool& pool) {
  auto& arena = *pool.arena;
  std::unique_ptr<Pool> retired;
  {
    std::lock_guard lock(arena.state->registry);
    if (pool.state != Pool::State::closed) return std::unexpected(Error::conflict);
    assert(!pool.lane && !pool.internal->notice_pending && !pool.capacity_changed.waiters &&
        !interested(pool));
    auto& pools = arena.state->pools;
    const auto selected = std::find_if(pools.begin(), pools.end(),
        [&pool](const auto& owner) { return owner.get() == &pool; });
    assert(selected != pools.end());
    retired = std::move(*selected);
    if (selected != pools.end() - 1) *selected = std::move(pools.back());
    pools.pop_back();
    if (pools.empty()) std::vector<std::unique_ptr<Pool>>{}.swap(pools);
  }
  return {};
}

} // namespace orbital::allocation
