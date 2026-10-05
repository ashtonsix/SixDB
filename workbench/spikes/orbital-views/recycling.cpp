#include "recycling.h"
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace recycling {
std::size_t page_size() { static const auto n = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); return n; }
struct Bucket;
struct Slot {
  std::byte* data{};
  std::size_t size{};
  bool restore_write{};
  Bucket* bucket{};
  std::atomic<unsigned> users{};
  Hint hint;
  Slot* next{};
};
struct Bucket {
  std::array<Slot*, 3> free{};
  std::size_t cold_count{};
};
struct State {
  std::mutex mutex;
  std::condition_variable wake;
  std::map<std::size_t, Bucket> buckets;
  Counts counts;
  bool stopping{}, trimming{}, working{};
  std::size_t keep{1};
  std::function<void()> hook;
  std::thread reclaimer;
  State() : reclaimer([this] { run(); }) {}
  ~State() {
    { std::lock_guard lock(mutex); stopping = true; } wake.notify_all(); reclaimer.join();
    for (auto& [size, bucket] : buckets) for (auto head : bucket.free)
      while (head) { auto* next = head->next; ::munmap(head->data, size); delete head; head = next; }
  }
  Slot** trim_slot() {
    if (!trimming) return nullptr;
    for (auto& [size, bucket] : buckets) {
      (void)size;
      if (bucket.cold_count > keep && (bucket.free[0] || bucket.free[1])) return &bucket.free[2];
    }
    return nullptr;
  }
  bool can_trim() { return trim_slot() != nullptr; }
  void run() {
    std::unique_lock lock(mutex);
    for (;;) {
      wake.wait(lock, [&] { return stopping || can_trim(); });
      if (stopping) return;
      auto** selected = trim_slot();
      Slot* slot = *selected; *selected = slot->next; --slot->bucket->cold_count; --counts.free; working = true;
      auto before = hook;
      lock.unlock();
      if (before) before();
      const int result = ::munmap(slot->data, slot->size); assert(result == 0);
      delete slot;
      lock.lock();
      ++counts.unmaps; working = false; wake.notify_all();
    }
  }
  void put(Slot* slot) {
    std::lock_guard lock(mutex);
    const auto index = static_cast<std::size_t>(slot->hint.temperature);
    auto& bucket = *slot->bucket;
    slot->next = bucket.free[index]; bucket.free[index] = slot;
    if (index == 2) ++bucket.cold_count;
    ++counts.free; --counts.held;
    if (trimming) wake.notify_all();
  }
};
Lease::Lease(std::shared_ptr<State> state, Slot* slot) : state_(std::move(state)), slot_(slot) {}
Lease::Lease(const Lease& other) : state_(other.state_), slot_(other.slot_) {
  if (slot_) slot_->users.fetch_add(1, std::memory_order_relaxed);
}
Lease::Lease(Lease&& other) noexcept : state_(std::move(other.state_)), slot_(std::exchange(other.slot_, nullptr)) {}
Lease::~Lease() { close(); }
std::byte* Lease::data() const { assert(slot_); return slot_->data; }
std::size_t Lease::size() const { assert(slot_); return slot_->size; }
Hint Lease::hint() const { assert(slot_); std::lock_guard lock(state_->mutex); return slot_->hint; }
void Lease::mapping_changed() { assert(slot_); std::lock_guard lock(state_->mutex); slot_->restore_write = true; }
void Lease::close() {
  if (slot_ && slot_->users.fetch_sub(1, std::memory_order_acq_rel) == 1) state_->put(slot_);
  slot_ = nullptr; state_.reset();
}
void Lease::release(Temperature temperature, int cpu) {
  release(Hint{temperature, cpu, std::chrono::steady_clock::now()});
}
void Lease::release(Hint hint) {
  assert(slot_);
  { std::lock_guard lock(state_->mutex); if (hint.last_access >= slot_->hint.last_access) slot_->hint = hint; }
  close();
}
Pool::Pool() : state_(std::make_shared<State>()) {}
Lease Pool::acquire(Temperature request, int cpu, std::size_t bytes) {
  assert(bytes && bytes % page_size() == 0);
  std::unique_lock lock(state_->mutex);
  auto& bucket = state_->buckets.try_emplace(bytes).first->second;
  const std::array<std::array<unsigned, 3>, 3> order{{{0, 1, 2}, {1, 0, 2}, {2, 1, 0}}};
  Slot* slot{};
  for (auto index : order[static_cast<unsigned>(request)]) {
    auto& head = bucket.free[index];
    if (!head) continue;
    // Prefer a HOT home on the requested CPU; otherwise its warmth is only a hint.
    Slot** selected = nullptr;
    auto** it = &head;
    for (unsigned n = 0; *it && n < 8; ++n, it = &(*it)->next) {
      if (!selected) selected = it;
      if (index != 0 || (*it)->hint.cpu == cpu) { selected = it; break; }
    }
    if (!selected) continue;
    slot = *selected; *selected = slot->next; slot->next = nullptr; --state_->counts.free;
    if (index == 2) --bucket.cold_count;
    ++state_->counts.reused;
    if (index != static_cast<unsigned>(request) || (index == 0 && slot->hint.cpu != cpu)) ++state_->counts.fallback;
    break;
  }
  if (!slot) {
    // Mapping happens off the pool latch. It is charged to preparation, never release.
    lock.unlock();
    auto fresh = std::make_unique<Slot>();
    fresh->size = bytes;
    fresh->bucket = &bucket;
    fresh->data = static_cast<std::byte*>(::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)); assert(fresh->data != MAP_FAILED);
    lock.lock(); slot = fresh.release(); ++state_->counts.maps;
  }
  slot->users.store(1, std::memory_order_relaxed); ++state_->counts.held;
  if (slot->restore_write) {
    slot->restore_write = false; ++state_->counts.restored;
    lock.unlock();
    const int result = ::mprotect(slot->data, slot->size, PROT_READ | PROT_WRITE); assert(result == 0);
  }
  return Lease(state_, slot);
}
Counts Pool::counts() const { std::lock_guard lock(state_->mutex); return state_->counts; }
void Pool::trim_cold(std::size_t keep) {
  { std::lock_guard lock(state_->mutex); state_->trimming = true; state_->keep = keep; }
  state_->wake.notify_all();
}
void Pool::wait_idle() {
  std::unique_lock lock(state_->mutex);
  state_->wake.wait(lock, [&] { return !state_->working && !state_->can_trim(); });
}
void Pool::reclaim_hook(std::function<void()> hook) { std::lock_guard lock(state_->mutex); state_->hook = std::move(hook); }
}
