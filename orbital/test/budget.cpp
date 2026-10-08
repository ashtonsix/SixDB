#include <orbital/allocation.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <pthread.h>
#include <source_location>
#include <thread>
#include <unistd.h>

namespace {
using namespace orbital;
namespace alloc = orbital::allocation;
using Clock = std::chrono::steady_clock;
void check(bool ok, const char* why,
    std::source_location where = std::source_location::current()) noexcept {
  if (ok) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), why);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class T> T take(Result<T> value) { CHECK(value); return std::move(*value); }

template<class Predicate> void await(Predicate ready) {
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!ready()) { CHECK(Clock::now() < deadline); std::this_thread::yield(); }
}

// These deadlines terminate failed liveness, not a measured latency promise.
struct Watchdog {
  std::mutex mutex;
  std::condition_variable changed;
  bool finished{};
  std::thread native{[this] {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(30), [&] { return finished; })) std::abort();
  }};
  ~Watchdog() {
    { std::lock_guard lock(mutex); finished = true; }
    changed.notify_one(); native.join();
  }
};

enum class Boundary { before_trylock, acquired_registry, refused_and_enrolled };

struct Gate {
  Boundary boundary;
  std::atomic<bool> entered{}, proceed{};
  void pause() noexcept {
    entered.store(true, std::memory_order_release);
    proceed.wait(false, std::memory_order_acquire);
  }
};

thread_local Gate* armed{};

struct Endpoint {
  worker::Lane lane;
  std::atomic<std::uint64_t> posts{};
  std::uint64_t handled{};
  Endpoint() { lane.notify_posted = posted; lane.notify_context = this; }
  static void posted(void* context) noexcept {
    static_cast<Endpoint*>(context)->posts.fetch_add(1, std::memory_order_release);
  }
  void poll() { handled += worker::poll(lane, 256); }
};

void close_remove(alloc::Pool& pool, Endpoint& endpoint) {
  await([&] {
    const auto result = alloc::close(pool);
    CHECK(result.kind() != Progress::Kind::failed);
    if (result.kind() == Progress::Kind::done) return true;
    endpoint.poll(); return false;
  });
  const auto stats = alloc::statistics(pool);
  CHECK(!pool.lane && stats.mapped_bytes == 0 && stats.metadata_bytes == 0 && stats.reserved_bytes == 0);
  CHECK(alloc::remove_pool(pool));
  CHECK(!worker::pending(endpoint.lane));
  worker::relinquish(endpoint.lane);
}

struct Waiter {
  WaitLink link;
  unsigned calls{};
  void enroll(Signal& signal, std::uint64_t observed) {
    CHECK(signal.generation == observed && !signal.waiters);
    link = {&signal, nullptr, this, [](void* context) noexcept {
      auto& self = *static_cast<Waiter*>(context);
      CHECK(!self.link.source && !self.link.next); ++self.calls;
    }};
    signal.waiters = &link;
  }
  ~Waiter() { CHECK(!link.source && !link.next); }
};

void race(Boundary boundary) {
  constexpr std::size_t body = 1u << 20;
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  CHECK(page > 3072 && body + 4 * page < 2 * body);
  auto arena = take(alloc::open_arena(body + 4 * page));
  Endpoint donor, requester; // Wake contexts outlive all publications and helper join.
  CHECK(worker::adopt(donor.lane));
  auto& pool = *take(alloc::add_pool(*arena, donor.lane));
  const alloc::AllocationRequest request{.bytes = body, .reserved_bytes = body,
      .alignment = 64, .initialization = alloc::Initialization::zero};
  auto occupied = take(alloc::allocate(pool, request));
  CHECK(arena->charged_bytes.load() >= body && arena->charged_bytes.load() <= arena->byte_limit);
  Gate gate{boundary};
  std::atomic<bool> ready{}, start{}, admitted{}, cleanup{};
  std::uint64_t generation{};
  unsigned callbacks{};
  std::thread owner([&] {
    CHECK(worker::adopt(requester.lane));
    auto& receiving = *take(alloc::add_pool(*arena, requester.lane));
    ready.store(true, std::memory_order_release);
    start.wait(false, std::memory_order_acquire);
    Waiter waiter;
    alloc::Allocation value;
    if (boundary == Boundary::refused_and_enrolled) {
      const auto observed = receiving.capacity_changed.generation;
      const auto denied = alloc::allocate(receiving, request);
      CHECK(!denied && denied.error() == Error::capacity && requester.posts.load() == 0);
      waiter.enroll(receiving.capacity_changed, observed);
      gate.pause();
      await([&] { requester.poll(); return waiter.calls == 1; });
      CHECK(!receiving.capacity_changed.waiters);
      value = take(alloc::allocate(receiving, request));
    } else {
      // The empty receiving Pool has no earlier inventory/registry attempt;
      // this first trylock follows its real failed shared-byte claim.
      armed = &gate;
      value = take(alloc::allocate(receiving, request));
      CHECK(!armed);
    }
    CHECK(value.size == body && value.address[0] == std::byte{0} && value.address[body - 1] == std::byte{0});
    value.address[0] = std::byte{0x41}; value.address[body - 1] = std::byte{0x72};
    CHECK(alloc::statistics(receiving).allocations == 1);
    generation = receiving.capacity_changed.generation; callbacks = waiter.calls;
    admitted.store(true, std::memory_order_release);
    cleanup.wait(false, std::memory_order_acquire);
    CHECK(value.address[0] == std::byte{0x41} && value.address[body - 1] == std::byte{0x72});
    alloc::release(value, {.temperature = alloc::Temperature::cold});
    close_remove(receiving, requester);
  });
  await([&] { return ready.load(std::memory_order_acquire); });
  start.store(true, std::memory_order_release); start.notify_one();
  await([&] { return gate.entered.load(std::memory_order_acquire); });
  // A real helper unmaps, then the donor owner refunds both payload and metadata.
  // In the acquired case the other owner still physically holds registry here.
  alloc::release(occupied, {.temperature = alloc::Temperature::cold});
  CHECK(alloc::trim_cold(pool, 0).kind() == Progress::Kind::waiting);
  await([&] { donor.poll(); return arena->charged_bytes.load() == 0 &&
      alloc::statistics(pool).mapped_bytes == 0 && alloc::statistics(pool).metadata_bytes == 0; });
  if (boundary == Boundary::refused_and_enrolled)
    // The requester owner is paused and cannot consume this linked notice.
    await([&] { donor.poll(); return requester.posts.load(std::memory_order_acquire) == 1; });
  else CHECK(requester.posts.load() == 0);
  CHECK(!gate.proceed.load());
  gate.proceed.store(true, std::memory_order_release); gate.proceed.notify_one();
  await([&] { donor.poll(); return admitted.load(std::memory_order_acquire); });
  const auto expected_notice = boundary == Boundary::refused_and_enrolled ? 1u : 0u;
  CHECK(requester.posts.load() == expected_notice && callbacks == expected_notice);
  CHECK((generation > 0) == bool(expected_notice));
  CHECK(arena->charged_bytes.load() >= body && arena->charged_bytes.load() <= arena->byte_limit);
  close_remove(pool, donor);
  cleanup.store(true, std::memory_order_release); cleanup.notify_one();
  owner.join();
  CHECK(arena->charged_bytes.load() == 0);
  // post callbacks may finish after the queue drains. Join the Arena helper
  // before Endpoint destruction; an empty queue alone is not quiescence.
  arena.reset();
  std::printf("case=refund-registration-boundary boundary=%s requester_notices=%u callbacks=%u admitted=1 verified=1 performance_claim=0\n",
      boundary == Boundary::before_trylock ? "before-trylock" :
      boundary == Boundary::acquired_registry ? "registry-held-before-enroll" : "after-refused-enrolled",
      expected_notice, callbacks);
}
} // namespace

extern "C" int __real_pthread_mutex_trylock(pthread_mutex_t*) noexcept;

extern "C" int __wrap_pthread_mutex_trylock(pthread_mutex_t* mutex) noexcept {
  auto* gate = armed;
  if (!gate) return __real_pthread_mutex_trylock(mutex);
  armed = nullptr;
  if (gate->boundary == Boundary::before_trylock) gate->pause();
  const auto result = __real_pthread_mutex_trylock(mutex);
  CHECK(result == 0);
  if (gate->boundary == Boundary::acquired_registry) gate->pause();
  return result;
}

int main() {
  Watchdog watchdog;
  race(Boundary::before_trylock);
  race(Boundary::acquired_registry);
  race(Boundary::refused_and_enrolled);
  std::puts("summary=deterministic-registration-history-proofs-passed weak_memory_proof=0 performance_claim=0");
}
