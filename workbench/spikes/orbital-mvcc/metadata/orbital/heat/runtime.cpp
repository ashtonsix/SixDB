// Actual Group-advice treatment checks; compile against the private patched tree.
#include "../shape/native_fixture.h"
#include "internal.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <source_location>
#include <thread>

namespace oa = orbital::allocation;
namespace d = oa::detail;
using Clock = std::chrono::steady_clock;
const auto stamp = Clock::time_point{Clock::duration{INT64_C(1) << 55}};
thread_local bool count_allocations{}, publisher{};
thread_local unsigned allocations{};
void* operator new(std::size_t n) { if (count_allocations) ++allocations; if (auto* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc{}; }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

template<class T> void check(const T& value, std::source_location location = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "runtime heat check failed at %s:%u\n", location.file_name(), location.line()); std::abort();
}
template<class T> T take(orbital::Result<T> r) { check(r); return std::move(*r); }
void await(std::atomic<bool>& flag) {
  const auto until = Clock::now() + std::chrono::seconds(5);
  while (!flag.load(std::memory_order_acquire)) { check(Clock::now() < until); std::this_thread::yield(); }
}
std::atomic<pthread_mutex_t*> target{};
std::atomic<bool> pause_lock{}, locked{}, resume{}, between{}, go{}, done{};
unsigned try_calls{};
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t*);
extern "C" int __real_pthread_mutex_trylock(pthread_mutex_t*);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t* mutex) {
  const auto result = __real_pthread_mutex_lock(mutex);
  if (!result && publisher && mutex == target.load() && pause_lock.exchange(false)) {
    locked.store(true, std::memory_order_release); await(resume);
  }
  return result;
}
extern "C" int __wrap_pthread_mutex_trylock(pthread_mutex_t* mutex) {
  if (!publisher && between.load() && mutex == target.load() && ++try_calls == 2) {
    go.store(true, std::memory_order_release); await(done);
  }
  return __real_pthread_mutex_trylock(mutex);
}
void foreign_release(oa::Hold& held, oa::ReleaseHint hint) {
  publisher = true; allocations = 0; count_allocations = true;
  oa::release(held, hint);
  count_allocations = false; check(allocations == 0); publisher = false;
}
void drain(compact_shape::probe::NativeFixture& f) {
  const auto until = Clock::now() + std::chrono::seconds(5);
  while (orbital::worker::pending(f.lane)) {
    check(Clock::now() < until); orbital::worker::poll(f.lane, 32);
  }
}

void partial_and_lifetime() {
  compact_shape::probe::NativeFixture f; check(f.open());
  auto allocation = take(oa::allocate(*f.pool, {.bytes = 64, .alignment = 1}));
  auto& group = *static_cast<d::Group*>(allocation.storage->domain->context);
  auto held = take(oa::hold(allocation, {0, 64}));
  auto extra = take(oa::hold(allocation, {0, 64}));
  oa::release(extra, {.temperature = oa::Temperature::hot, .last_access = stamp});
  check(group.last_use.temperature == oa::Temperature::hot && group.last_use.last_access == stamp);
  check(!group.domain.queued.load());
  std::thread foreign([&] { foreign_release(held, {.temperature = {}, .last_access = stamp + Clock::duration{1}}); });
  foreign.join();
  check(group.domain.queued.load() && group.domain.lifetime && group.advice_pending);
  check(group.uses[allocation.storage->identity].users.load() == 1);
  oa::release(allocation); // Queue pin must survive owner-local final release.
  check(group.active_identities == 0 && group.domain.lifetime);
  drain(f);
  check(group.last_use.temperature == oa::Temperature::hot && group.last_use.last_access == stamp + Clock::duration{1});
  check(!group.domain.queued.load() && !group.domain.lifetime && !group.advice_pending);
  // A different identity's stale supplied timestamp is deliberately Group-stale.
  auto second = take(oa::allocate(*f.pool, {.bytes = 48, .alignment = 1}));
  check(second.storage->domain->context == &group);
  oa::release(second, {.temperature = oa::Temperature::cold, .last_access = stamp});
  check(group.last_use.temperature == oa::Temperature::hot);
  auto third = take(oa::allocate(*f.pool, {.bytes = 48, .alignment = 1}));
  allocations = 0; count_allocations = true;
  oa::release(third, {.temperature = oa::Temperature::cold, .last_access = stamp + Clock::duration{2}});
  count_allocations = false; check(allocations == 0);
  check(group.last_use.temperature == oa::Temperature::cold && !group.domain.queued.load());
  std::printf("{\"case\":\"runtime-group\",\"group_size\":%zu,\"pool_metadata\":%llu,\"release_cpp_allocations\":0}\n",
      sizeof(group), static_cast<unsigned long long>(f.statistics().metadata_bytes));
}

void stalled_publisher() {
  compact_shape::probe::NativeFixture f; check(f.open());
  auto allocation = take(oa::allocate(*f.pool, {.bytes = 64, .alignment = 1}));
  auto& group = *static_cast<d::Group*>(allocation.storage->domain->context);
  auto first = take(oa::hold(allocation, {0, 64}));
  std::thread initial([&] { foreign_release(first, {oa::Temperature::warm}); }); initial.join();
  auto final = take(oa::hold(allocation, {0, 64}));
  target.store(group.domain.mutex.native_handle()); pause_lock.store(true); locked.store(false); resume.store(false);
  std::thread foreign([&] { foreign_release(final, {.temperature = oa::Temperature::hot, .last_access = stamp}); });
  await(locked);
  orbital::worker::poll(f.lane, 1); // Both callback try-locks fail; existing pin/route survives.
  check(group.domain.queued.load() && group.domain.lifetime);
  oa::release(allocation);
  resume.store(true, std::memory_order_release); foreign.join(); target.store(nullptr);
  drain(f);
  check(group.last_use.temperature == oa::Temperature::hot && group.last_use.last_access == stamp);
  check(!group.domain.queued.load() && group.active_identities == 0);
}

void between_handle_and_release() {
  compact_shape::probe::NativeFixture f; check(f.open());
  auto allocation = take(oa::allocate(*f.pool, {.bytes = 64, .alignment = 1}));
  auto& group = *static_cast<d::Group*>(allocation.storage->domain->context);
  auto first = take(oa::hold(allocation, {0, 64}));
  auto next = take(oa::hold(allocation, {0, 64}));
  std::thread initial([&] { foreign_release(first, {.temperature = oa::Temperature::hot, .last_access = stamp}); }); initial.join();
  target.store(group.domain.mutex.native_handle()); try_calls = 0; go.store(false); done.store(false); between.store(true);
  std::thread foreign([&] {
    await(go); foreign_release(next, {.temperature = oa::Temperature::warm, .last_access = stamp + Clock::duration{1}});
    done.store(true, std::memory_order_release);
  });
  orbital::worker::poll(f.lane, 1);
  between.store(false); foreign.join(); target.store(nullptr);
  check(group.last_use.temperature == oa::Temperature::hot && group.advice_pending && group.domain.queued.load());
  drain(f);
  check(group.last_use.temperature == oa::Temperature::warm && group.last_use.last_access == stamp + Clock::duration{1});
  check(!group.domain.queued.load()); oa::release(allocation);
}

int main() { partial_and_lifetime(); stalled_publisher(); between_handle_and_release(); std::puts("{\"case\":\"runtime-histories\",\"passed\":true}"); }
