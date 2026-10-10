#include <orbital/allocation.h>
#include "finite.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace orbital;
using namespace orbital::allocation;
using Clock = std::chrono::steady_clock;
constexpr auto timeout = std::chrono::seconds(10);

void check(bool condition, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
    std::abort();
  }
}

template<class T> T take(Result<T> value,
    std::source_location where = std::source_location::current()) {
  check(value.has_value(), "operation rejected", where);
  return std::move(*value);
}

template<class Predicate> void await(Predicate ready, const char* reason) {
  const auto deadline = Clock::now() + timeout;
  while (!ready()) {
    check(Clock::now() < deadline, reason);
    std::this_thread::yield();
  }
}

enum class Call { discard, protect, unmap };

enum class Action { fail, pause, fail_preparation };

enum class Decision { normal, fail, paused, partial_preparation };

thread_local bool observe_vm{};
thread_local unsigned observed_vm_calls{};
thread_local bool fail_metadata{}, metadata_reservation_seen{};
thread_local std::size_t metadata_size{};

void note_vm() noexcept { if (observe_vm) ++observed_vm_calls; }

struct Hook {
  std::mutex mutex;
  std::condition_variable changed;
  std::thread::id owner;
  Call call{};
  Action action{};
  void* address{};
  bool armed{}, entered{}, resume{}, finished{};
} hook;

void arm(Call call, Action action, void* address = nullptr) {
  std::lock_guard lock(hook.mutex);
  check(!hook.armed && (!hook.entered || hook.finished), "previous syscall hook still active");
  hook.owner = std::this_thread::get_id();
  hook.call = call; hook.action = action; hook.address = address;
  hook.armed = true; hook.entered = hook.resume = hook.finished = false;
}

Decision before(Call call, void* address, bool reclamation = true, bool preparation = false) {
  std::unique_lock lock(hook.mutex);
  const bool partial = hook.action == Action::fail_preparation;
  const bool caller = hook.owner == std::this_thread::get_id();
  if (!hook.armed || hook.call != call || !(partial ? preparation : reclamation) ||
      caller != partial ||
      (hook.address && hook.address != address)) return Decision::normal;
  hook.armed = false;
  hook.entered = true;
  hook.changed.notify_all();
  if (partial) return Decision::partial_preparation;
  if (hook.action == Action::fail) {
    hook.finished = true;
    errno = EIO;
    return Decision::fail;
  }
  check(hook.changed.wait_until(lock, Clock::now() + timeout, [] { return hook.resume; }),
      "foreground did not release paused reclamation");
  return Decision::paused;
}

void after(Decision decision) {
  if (decision != Decision::paused && decision != Decision::partial_preparation) return;
  std::lock_guard lock(hook.mutex);
  hook.finished = true;
  hook.changed.notify_all();
}

void wait_entered() {
  std::unique_lock lock(hook.mutex);
  check(hook.changed.wait_until(lock, Clock::now() + timeout, [] { return hook.entered; }),
      "reclaimer did not reach requested syscall");
}

void resume_reclamation() {
  std::lock_guard lock(hook.mutex);
  hook.resume = true;
  hook.changed.notify_all();
}

void wait_finished() {
  std::unique_lock lock(hook.mutex);
  check(hook.changed.wait_until(lock, Clock::now() + timeout, [] { return hook.finished; }),
      "reclaimer did not finish actual syscall");
}

struct Fixture {
  Ref<Arena> arena;
  worker::Lane lane;
  Pool* pool;
  explicit Fixture(std::uint64_t limit = 16 * 1024 * 1024)
      : arena(take(open_arena(limit))) {
    check(worker::adopt(lane).has_value(), "adopt caller lane");
    pool = take(add_pool(*arena, lane));
  }
  ~Fixture() {
    check(pool->state == Pool::State::closed, "Pool was not closed");
    check(!worker::pending(lane), "messages survived Pool shutdown");
    worker::relinquish(lane);
  }
};

template<class Step> Progress finish(worker::Lane& lane, Step step) {
  Progress result = Progress::yield();
  await([&] {
    result = step();
    if (result.kind() == Progress::Kind::done || result.kind() == Progress::Kind::failed) return true;
    worker::poll(lane, 16);
    return false;
  }, "allocation service failed to make progress");
  return result;
}

void trim(Fixture& fixture, std::uint64_t keep = 0) {
  check(finish(fixture.lane, [&] { return trim_cold(*fixture.pool, keep); }).kind() == Progress::Kind::done,
      "unexpected trim failure");
}

void close_fixture(Fixture& fixture) {
  const auto result = finish(fixture.lane, [&] { return close(*fixture.pool); });
  if (result.kind() != Progress::Kind::done)
    std::fprintf(stderr, "close failure code %u\n", static_cast<unsigned>(result.failure()));
  check(result.kind() == Progress::Kind::done, "unexpected close failure");
  check(fixture.arena->charged_bytes.load() == 0, "closed Pool retained a budget charge");
}

void expect_io(Progress result) {
  check(result.kind() == Progress::Kind::failed && result.failure() == Error::io,
      "reclamation failure was lost or reported as success");
  wait_finished();
}

void unchanged_inventory(const Statistics& before, const Statistics& after) {
  check(after.mapped_bytes == before.mapped_bytes &&
      after.reserved_bytes == before.reserved_bytes &&
      after.metadata_bytes == before.metadata_bytes,
      "unsettled reclamation changed charged inventory");
  check(after.trim_pages == before.trim_pages, "unsettled reclamation counted trimmed pages");
}

void finite_payload_preparation_and_cleanup_failure() {
  Fixture fixture;
  arm(Call::protect, Action::fail_preparation);
  auto refused = allocate(*fixture.pool, {64, 0, 16, 0, Initialization::zero});
  check(!refused && refused.error() == Error::io, "partial payload preparation did not fail");
  wait_finished();
  const auto before = statistics(*fixture.pool);
  const auto charged = fixture.arena->charged_bytes.load();
  check(before.allocations == 0 && before.mapped_bytes == 0 && before.reserved_bytes > 0 &&
      before.metadata_bytes > 0 && charged == before.metadata_bytes,
      "failed preparation lost ownership or exposed payload inventory");
  arm(Call::unmap, Action::fail);
  expect_io(finish(fixture.lane, [&] { return trim_cold(*fixture.pool, 0); }));
  check(fixture.pool->state == Pool::State::open && fixture.pool->lane == &fixture.lane,
      "failed preparation cleanup lost its owner route");
  check(fixture.arena->charged_bytes.load() == charged, "failed cleanup refunded quarantined backing");
  unchanged_inventory(before, statistics(*fixture.pool));
  trim(fixture);
  const auto after = statistics(*fixture.pool);
  check(after.mapped_bytes == 0 && after.reserved_bytes == 0 && after.metadata_bytes == 0 &&
      fixture.arena->charged_bytes.load() == 0, "cleanup retry stranded reservation or metadata");
  check(after.trim_pages == before.trim_pages, "unconfirmed payload cleanup was counted as trimmed payload");
  auto value = take(allocate(*fixture.pool, {64, 0, 16, 0, Initialization::zero}));
  for (auto byte : bytes(value)) check(byte == std::byte{0}, "new finite allocation exposed failed preparation data");
  release(value); close_fixture(fixture);
}

void finite_heap_preparation_and_cleanup_failure(std::size_t alignment) {
  Fixture fixture;
  const auto references = fixture.arena->references.load();
  auto planned = take(detail::FiniteGroup::create({alignment >= 64 ? detail::Palette::b : detail::Palette::a, 256}));
  metadata_size = planned->control_bytes();
  metadata_reservation_seen = false; fail_metadata = true;
  auto refused = allocate(*fixture.pool, {64, 0, alignment, 0, Initialization::zero});
  check(!refused && refused.error() == Error::capacity && !fail_metadata && metadata_reservation_seen,
        "metadata ENOMEM after payload reservation was lost");
  const auto before = statistics(*fixture.pool);
  const auto charged = fixture.arena->charged_bytes.load();
  check(before.allocations == 0 && before.mapped_bytes == 0 && before.preparing_bytes == 0 &&
      before.uncertain_bytes == 0 && before.reserved_bytes > 0 &&
      before.metadata_bytes > 0 && charged == before.metadata_bytes,
      "failed preparation lost ownership or exposed payload inventory");
  check(fixture.arena->references.load() == references, "heap failure exposed an active allocation pin");
  arm(Call::unmap, Action::fail);
  expect_io(finish(fixture.lane, [&] { return trim_cold(*fixture.pool, 0); }));
  check(fixture.pool->state == Pool::State::open && fixture.pool->lane == &fixture.lane,
      "failed preparation cleanup lost its owner route");
  check(fixture.arena->charged_bytes.load() == charged, "failed cleanup refunded quarantined backing");
  unchanged_inventory(before, statistics(*fixture.pool));
  check(fixture.arena->references.load() == references, "failed cleanup retained a transient trim pin");
  trim(fixture);
  const auto after = statistics(*fixture.pool);
  check(after.mapped_bytes == 0 && after.reserved_bytes == 0 && after.metadata_bytes == 0 &&
      fixture.arena->charged_bytes.load() == 0, "cleanup retry stranded reservation or metadata");
  check(after.trim_pages == before.trim_pages, "control cleanup was counted as trimmed payload");
  check(fixture.arena->references.load() == references, "successful retry retained a transient pin");
  auto value = take(allocate(*fixture.pool, {64, 0, 16, 0, Initialization::zero}));
  for (auto byte : bytes(value)) check(byte == std::byte{0}, "new finite allocation exposed failed preparation data");
  release(value); close_fixture(fixture);
}

void budget_refusal_before_vm() {
  Fixture fixture(1);
  for (const auto size : {std::size_t{64}, std::size_t{8192}}) {
    observed_vm_calls = 0;
    observe_vm = true;
    auto refused = allocate(*fixture.pool, {size, 0, 16});
    observe_vm = false;
    check(!refused && refused.error() == Error::capacity, "tight budget did not refuse allocation");
    check(observed_vm_calls == 0, "budget refusal performed foreground VM setup or cleanup");
    const auto state = statistics(*fixture.pool);
    check(state.mapped_bytes == 0 && state.reserved_bytes == 0 && state.metadata_bytes == 0 &&
        fixture.arena->charged_bytes.load() == 0, "budget refusal left allocation inventory");
  }
  close_fixture(fixture);
}

Allocation finite_neighbor(Fixture& fixture) {
  auto live = take(allocate(*fixture.pool, {3072, 0, 16, 0, Initialization::zero}));
  const auto original = statistics(*fixture.pool).mapped_bytes;
  std::vector<Allocation> others;
  while (statistics(*fixture.pool).mapped_bytes == original)
    others.push_back(take(allocate(*fixture.pool, {3072, 0, 16})));
  others.clear();
  return live;
}

void finite_discard_failure(Call call) {
  Fixture fixture;
  // Populate a second payload page, then retain only the first home's shared page.
  auto live = finite_neighbor(fixture);
  std::memset(live.address, 0x71, live.size);
  const auto* address = live.address;
  const auto charged = fixture.arena->charged_bytes.load();
  const auto before = statistics(*fixture.pool);
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  check(before.idle_bytes == page, "finite setup did not isolate one idle payload page");
  arm(call, Action::fail);
  expect_io(finish(fixture.lane, [&] { return trim_cold(*fixture.pool, 0); }));
  check(fixture.pool->lane == &fixture.lane && fixture.pool->state == Pool::State::open,
      "failed trim detached the live Pool");
  check(fixture.arena->charged_bytes.load() == charged, "failed discard refunded backing");
  unchanged_inventory(before, statistics(*fixture.pool));
  check(live.address == address, "trim moved neighboring allocation");
  for (auto byte : bytes(live)) check(byte == std::byte{0x71}, "trim damaged live shared-page bytes");
  trim(fixture);
  const auto after = statistics(*fixture.pool);
  check(after.mapped_bytes == before.mapped_bytes - page &&
      after.trim_pages == before.trim_pages + 1, "retry did not account for the idle page");
  for (auto byte : bytes(live)) check(byte == std::byte{0x71}, "retry damaged live neighboring bytes");
  release(live); close_fixture(fixture);
}

void pool_wide_retention_floor() {
  Fixture fixture;
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  auto live = finite_neighbor(fixture);
  auto window = take(allocate(*fixture.pool, {std::max(page, std::size_t{4096}), 0, 64}));
  release(window);
  const auto before = statistics(*fixture.pool);
  check(before.idle_bytes == 2 * page, "finite and virtual idle inventory was not counted together");
  trim(fixture, page);
  const auto after = statistics(*fixture.pool);
  check(after.mapped_bytes == before.mapped_bytes - page && after.idle_bytes == page,
      "retention floor was applied separately to finite and virtual backing");
  release(live); close_fixture(fixture);
}

void window_unmap_failure() {
  Fixture fixture;
  auto value = take(allocate(*fixture.pool, {8192, 32768, 64, 0, Initialization::zero}));
  auto* address = value.address;
  release(value);
  const auto charged = fixture.arena->charged_bytes.load();
  const auto before = statistics(*fixture.pool);
  arm(Call::unmap, Action::fail, address);
  expect_io(finish(fixture.lane, [&] { return trim_cold(*fixture.pool, 0); }));
  check(fixture.pool->lane == &fixture.lane, "failed unmap destroyed completion route");
  check(fixture.arena->charged_bytes.load() == charged, "failed unmap refunded backing");
  unchanged_inventory(before, statistics(*fixture.pool));
  auto reused = take(allocate(*fixture.pool, {8192, 32768, 64, 0, Initialization::zero}));
  check(reused.address == address, "failed unmap lost reusable backing");
  for (auto byte : bytes(reused)) check(byte == std::byte{0}, "failed unmap broke initialization");
  release(reused); trim(fixture);
  check(statistics(*fixture.pool).mapped_bytes == 0, "actual retry retained window backing");
  close_fixture(fixture);
}

void whole_group_unmap_failure() {
  Fixture fixture;
  auto value = take(allocate(*fixture.pool, {64, 0, 16}));
  release(value);
  const auto charged = fixture.arena->charged_bytes.load();
  const auto before = statistics(*fixture.pool);
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  arm(Call::unmap, Action::fail);
  expect_io(finish(fixture.lane, [&] { return close(*fixture.pool); }));
  check(fixture.pool->lane == &fixture.lane && fixture.pool->state == Pool::State::closing,
      "failed group retirement destroyed the retry route");
  check(fixture.arena->charged_bytes.load() == charged, "failed group retirement refunded backing");
  unchanged_inventory(before, statistics(*fixture.pool));
  close_fixture(fixture);
  check(statistics(*fixture.pool).trim_pages == before.trim_pages + before.mapped_bytes / page,
      "whole-group retirement omitted trimmed payload pages");
}

void paused_reclaim() {
  Fixture fixture;
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  const auto size = std::max(page, std::size_t{4096});
  auto cold = take(allocate(*fixture.pool, {size, 0, 64}));
  auto warm = take(allocate(*fixture.pool, {size, 0, 64}));
  auto* cold_address = cold.address;
  auto* warm_address = warm.address;
  release(cold);
  release(warm, {Temperature::warm});
  const auto charged = fixture.arena->charged_bytes.load();
  const auto before = statistics(*fixture.pool);
  check(!worker::pending(fixture.lane), "unexpected setup message");
  arm(Call::unmap, Action::pause, cold_address);
  check(trim_cold(*fixture.pool, size).kind() == Progress::Kind::waiting, "trim did not start");
  wait_entered();
  auto reused = take(allocate(*fixture.pool, {size, 0, 64}));
  check(reused.address == warm_address, "paused VM work blocked compatible local reuse");
  std::memset(reused.address, 0x39, reused.size);
  check(fixture.arena->charged_bytes.load() == charged, "paused VM work refunded capacity early");
  unchanged_inventory(before, statistics(*fixture.pool));
  resume_reclamation(); wait_finished();
  await([&] { return worker::pending(fixture.lane); }, "physical completion was not posted");
  check(fixture.arena->charged_bytes.load() == charged,
      "background completion refunded capacity before owner Message handling");
  unchanged_inventory(before, statistics(*fixture.pool));
  worker::poll(fixture.lane, 16);
  check(fixture.arena->charged_bytes.load() <= charged - size, "handled completion did not return capacity");
  const auto after = statistics(*fixture.pool);
  check(after.mapped_bytes == before.mapped_bytes - size &&
      after.trim_pages == before.trim_pages + size / page, "Window completion inventory was inaccurate");
  for (auto byte : bytes(reused)) check(byte == std::byte{0x39}, "reclaim disturbed live reused storage");
  release(reused); close_fixture(fixture);
}

void paused_group_retirement() {
  Fixture fixture;
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  const auto size = std::max(page, std::size_t{4096});
  auto finite = take(allocate(*fixture.pool, {64, 0, 16}));
  release(finite);
  auto warm = take(allocate(*fixture.pool, {size, 0, 64}));
  auto* address = warm.address;
  release(warm, {Temperature::warm});
  const auto before = statistics(*fixture.pool);
  const auto charged = fixture.arena->charged_bytes.load();
  const auto finite_bytes = before.mapped_bytes - size;
  check(finite_bytes && before.idle_bytes == before.mapped_bytes,
      "whole-group setup has unexpected live inventory");
  arm(Call::unmap, Action::pause);
  check(trim_cold(*fixture.pool, size).kind() == Progress::Kind::waiting, "group retirement did not start");
  wait_entered();
  auto reused = take(allocate(*fixture.pool, {size, 0, 64}));
  check(reused.address == address, "whole-group retirement blocked unrelated local reuse");
  std::memset(reused.address, 0x4d, reused.size);
  unchanged_inventory(before, statistics(*fixture.pool));
  check(fixture.arena->charged_bytes.load() == charged, "paused group retirement refunded capacity");
  resume_reclamation(); wait_finished();
  await([&] { return worker::pending(fixture.lane); }, "retired group completion was not posted");
  unchanged_inventory(before, statistics(*fixture.pool));
  check(fixture.arena->charged_bytes.load() == charged,
      "retired group inventory disappeared before owner Message handling");
  worker::poll(fixture.lane, 16);
  const auto after = statistics(*fixture.pool);
  check(after.mapped_bytes == size && after.trim_pages == before.trim_pages + finite_bytes / page,
      "owner did not settle whole-group inventory");
  check(fixture.arena->charged_bytes.load() < charged, "settled retirement retained group budget");
  for (auto byte : bytes(reused)) check(byte == std::byte{0x4d}, "group retirement damaged unrelated live storage");
  release(reused); close_fixture(fixture);
}

void bounded_window_records() {
  Fixture fixture;
  constexpr std::array<std::size_t, 6> sizes{4096, 8192, 32768, 65536, 12288, 16384};
  std::uint64_t metadata_bound{};
  for (unsigned i = 0; i < 96; ++i) {
    auto value = take(allocate(*fixture.pool, {sizes[i % sizes.size()], 0, 64, 0, Initialization::zero}));
    check(value.address[0] == std::byte{0} && value.address[value.size - 1] == std::byte{0},
        "recreated window exposed stale endpoint bytes");
    value.address[0] = value.address[value.size - 1] = std::byte{0x53};
    if (!i) metadata_bound = statistics(*fixture.pool).metadata_bytes + 4096;
    release(value); trim(fixture);
    const auto state = statistics(*fixture.pool);
    check(state.mapped_bytes == 0 && state.reserved_bytes == 0, "trim left an idle virtual window mapped");
    check(state.metadata_bytes <= metadata_bound, "dead window records accumulated across reclamation");
    check(fixture.arena->charged_bytes.load() <= metadata_bound, "dead window records stranded budget");
  }
  close_fixture(fixture);
}

void shared_budget_and_live_neighbor() {
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  constexpr std::size_t large = 1024 * 1024;
  auto arena = take(open_arena(large + 4 * page));
  worker::Lane first_lane, second_lane;
  check(worker::adopt(first_lane).has_value() && worker::adopt(second_lane).has_value(), "adopt Pool lanes");
  auto& first = *take(add_pool(*arena, first_lane));
  auto& second = *take(add_pool(*arena, second_lane));
  auto value = take(allocate(first, {large, 0, 64, 0, Initialization::zero}));
  auto retained = take(hold(value, {0, large}));
  auto neighbor = take(allocate(second, {std::max(page, std::size_t{4096}), 0, 64, 0, Initialization::zero}));
  std::memset(neighbor.address, 0x6b, neighbor.size);
  release(value);
  auto denied = allocate(second, {large, 0, 64});
  check(!denied && denied.error() == Error::capacity, "Arena limit admitted overlapping backing");
  check(trim_cold(first, 0).kind() == Progress::Kind::done, "trim claimed a still-held window");
  retained.address[large - 1] = std::byte{0x45};
  std::thread releaser([held = std::move(retained)]() mutable { release(held); });
  releaser.join();
  while (worker::pending(first_lane)) worker::poll(first_lane, 16);
  while (worker::pending(second_lane)) worker::poll(second_lane, 16);
  bool woken = false;
  WaitLink waiter;
  waiter.source = &second.capacity_changed;
  waiter.waiter = &woken;
  waiter.notify = [](void* flag) noexcept { *static_cast<bool*>(flag) = true; };
  waiter.next = second.capacity_changed.waiters;
  second.capacity_changed.waiters = &waiter;
  const auto generation = second.capacity_changed.generation;
  check(finish(first_lane, [&] { return trim_cold(first, 0); }).kind() == Progress::Kind::done, "shared-budget trim failed");
  check(!woken && second.capacity_changed.generation == generation,
      "budget broadcast mutated the other Lane's Signal directly");
  await([&] { worker::poll(second_lane, 16); return woken; }, "other Pool never observed returned budget");
  check(!waiter.source, "capacity notification retained its wait link");
  auto admitted = take(allocate(second, {large, 0, 64}));
  for (auto byte : bytes(neighbor)) check(byte == std::byte{0x6b}, "another Pool's reclaim damaged live bytes");
  release(admitted); release(neighbor);
  check(finish(first_lane, [&] { return close(first); }).kind() == Progress::Kind::done, "first Pool close failed");
  check(finish(second_lane, [&] { return close(second); }).kind() == Progress::Kind::done, "second Pool close failed");
  check(arena->charged_bytes.load() == 0, "shared Arena retained closed Pool charges");
  worker::relinquish(second_lane); worker::relinquish(first_lane);
}

void close_during_foreign_returns() {
  for (unsigned round = 0; round < 16; ++round) {
    Fixture fixture;
    std::array<std::vector<Hold>, 4> batches;
    for (unsigned i = 0; i < 128; ++i) {
      auto value = take(allocate(*fixture.pool, {64, 0, 16}));
      std::memset(value.address, static_cast<int>(i), value.size);
      batches[i % batches.size()].push_back(take(hold(value, {0, 64})));
      release(value);
    }
    std::atomic<bool> start{};
    std::array<std::thread, 4> threads;
    for (unsigned n = 0; n < threads.size(); ++n) {
      threads[n] = std::thread([&, n, held = std::move(batches[n])]() mutable {
        start.wait(false, std::memory_order_acquire);
        for (std::size_t i = 0; i < held.size(); ++i) {
          const auto expected = std::byte{static_cast<unsigned char>(n + 4 * i)};
          check(held[i].address[0] == expected && held[i].address[63] == expected,
              "close reclaimed a foreign-held allocation");
          release(held[i], i % 2 ? ReleaseHint{Temperature::warm, Clock::now()} : ReleaseHint{});
          std::this_thread::yield();
        }
      });
    }
    check(close(*fixture.pool).kind() == Progress::Kind::waiting, "close ignored foreign-held backing");
    auto refused = allocate(*fixture.pool, {64, 0, 16});
    check(!refused && refused.error() == Error::unavailable, "closing Pool accepted a new allocation");
    start.store(true, std::memory_order_release); start.notify_all();
    // Poll and close while releasers are running; joining first hides the publication race.
    close_fixture(fixture);
    for (auto& thread : threads) thread.join();
  }
}
} // namespace

extern "C" int __real_madvise(void*, std::size_t, int) noexcept;
extern "C" int __real_mprotect(void*, std::size_t, int) noexcept;
extern "C" int __real_munmap(void*, std::size_t) noexcept;
extern "C" void* __real_mmap(void*, std::size_t, int, int, int, off_t) noexcept;
extern "C" void* __wrap_mmap(void* address, std::size_t size, int rights, int flags,
    int descriptor, off_t offset) noexcept {
  note_vm();
  auto* result = __real_mmap(address, size, rights, flags, descriptor, offset);
  if (fail_metadata && rights == PROT_NONE && result != MAP_FAILED) metadata_reservation_seen = true;
  return result;
}

extern "C" void* __real__ZnwmRKSt9nothrow_t(std::size_t, const std::nothrow_t&) noexcept;
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(std::size_t bytes, const std::nothrow_t& tag) noexcept {
  if (fail_metadata && bytes == metadata_size) {
    check(metadata_reservation_seen, "metadata allocation preceded actual payload reservation");
    fail_metadata = false;
    return nullptr;
  }
  return __real__ZnwmRKSt9nothrow_t(bytes, tag);
}

extern "C" int __wrap_madvise(void* address, std::size_t size, int advice) noexcept {
  note_vm();
  auto decision = before(Call::discard, address, advice == MADV_DONTNEED);
  if (decision == Decision::fail) return -1;
  const auto result = __real_madvise(address, size, advice);
  after(decision);
  return result;
}

extern "C" int __wrap_mprotect(void* address, std::size_t size, int rights) noexcept {
  note_vm();
  auto decision = before(Call::protect, address, rights == PROT_NONE, rights == (PROT_READ | PROT_WRITE));
  if (decision == Decision::fail) return -1;
  if (decision == Decision::partial_preparation) {
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    check(size >= page && __real_mprotect(address, page, rights) == 0,
        "could not make a real partial payload preparation");
    after(decision);
    errno = EIO;
    return -1;
  }
  const auto result = __real_mprotect(address, size, rights);
  after(decision);
  return result;
}

extern "C" int __wrap_munmap(void* address, std::size_t size) noexcept {
  note_vm();
  auto decision = before(Call::unmap, address);
  if (decision == Decision::fail) return -1;
  const auto result = __real_munmap(address, size);
  after(decision);
  return result;
}

int main() {
  budget_refusal_before_vm();
  finite_payload_preparation_and_cleanup_failure();
  finite_heap_preparation_and_cleanup_failure(16);
  finite_heap_preparation_and_cleanup_failure(64);
  finite_discard_failure(Call::discard);
  finite_discard_failure(Call::protect);
  pool_wide_retention_floor();
  window_unmap_failure();
  whole_group_unmap_failure();
  paused_reclaim();
  paused_group_retirement();
  bounded_window_records();
  shared_budget_and_live_neighbor();
  close_during_foreign_returns();
  std::puts("reclamation failures, asynchronous progress, capacity wakes and foreign close pass");
}
