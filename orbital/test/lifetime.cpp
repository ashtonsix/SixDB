#include <orbital/allocation.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

using namespace orbital;
using namespace orbital::allocation;

namespace {
std::atomic<std::uintptr_t> tracked_address{};
std::atomic<unsigned> successful_unmaps{};
void check(bool condition, const char* detail,
           std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), detail);
    std::abort();
  }
}

template<class T> T take(Result<T> value,
                        std::source_location where = std::source_location::current()) {
  check(bool(value), "operation refused", where);
  return std::move(*value);
}

std::byte expected(std::size_t index, unsigned salt) {
  return std::byte((index * 13 + salt) % 251);
}

void fill(Allocation& value, unsigned salt) {
  for (std::size_t index = 0; index < value.size; ++index)
    value.address[index] = expected(index, salt);
}

void verify(const Hold& value, std::size_t body, unsigned salt) {
  const auto captured = bytes(value);
  for (std::size_t index = 0; index < captured.size(); ++index) {
    const auto logical = value.footprint.offset + index;
    check(captured[index] == (logical < body ? expected(logical, salt) : std::byte{0}),
          "captured bytes or readable padding changed");
  }
}

void track(std::byte* address) {
  successful_unmaps.store(0, std::memory_order_relaxed);
  tracked_address.store(reinterpret_cast<std::uintptr_t>(address), std::memory_order_release);
}

void finish_close(Pool& pool, worker::Lane& lane) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto progress = close(pool);
    check(progress.kind() != Progress::Kind::failed, "Pool close failed");
    if (progress.kind() == Progress::Kind::done) return;
    check(std::chrono::steady_clock::now() < deadline, "Pool close did not progress");
    worker::poll(lane, 32);
    std::this_thread::yield();
  }
}

AllocationRequest request(std::size_t body) { return {body, body * 4, 64, 16, Initialization::zero}; }

void local_last_use(std::size_t body) {
  auto external = take(open_arena(16 * 1024 * 1024));
  worker::Lane lane;
  check(bool(worker::adopt(lane)), "adopt local Lane");
  auto& pool = *take(add_pool(*external, lane));
  auto value = take(allocate(pool, request(body)));
  fill(value, 17);
  track(value.address);
  auto first = take(hold(value, {3, 35}));
  auto copy = retain(first);
  auto entire = take(hold(value, {0, body + 16}));
  release(value);
  external.reset();
  auto late = retain(first);
  verify(first, body, 17); verify(copy, body, 17); verify(entire, body, 17);
  verify(late, body, 17);
  check(successful_unmaps.load() == 0, "dropping logical owners reclaimed captured storage");
  release(first);
  auto moved = std::move(copy);
  verify(moved, body, 17);
  release(entire);
  release(late);
  check(successful_unmaps.load() == 0, "retained Hold did not preserve physical lifetime");
  release(moved);
  // The sole physical pin may destroy Arena and Pool here. Observe actual VM
  // retirement, and never dereference those raw records after this boundary.
  check(successful_unmaps.load() == 1, "last local Hold leaked or lost its Arena lifetime");
  check(!worker::pending(lane), "last local use left an undrainable Message");
  tracked_address.store(0, std::memory_order_release);
  worker::relinquish(lane);
}

void foreign_last_use(std::size_t body, bool reuse) {
  auto external = take(open_arena(16 * 1024 * 1024));
  auto* arena = external.get();
  worker::Lane lane;
  check(bool(worker::adopt(lane)), "adopt foreign-return Lane");
  auto& pool = *take(add_pool(*arena, lane));
  auto value = take(allocate(pool, request(body)));
  fill(value, 41);
  auto* original = value.address;
  track(original);
  auto first = take(hold(value, {0, body + 16}));
  auto last = retain(first);
  release(value); release(first);
  external.reset();
  std::thread foreign([owned = std::move(last), body]() mutable {
    verify(owned, body, 41);
    release(owned);
  });
  foreign.join();
  check(worker::pending(lane), "final foreign use did not retain a return route");
  check(successful_unmaps.load() == 0, "final foreign publication reclaimed storage before owner drain");
  // The queued return owns the old physical pin and its stable route. Recover
  // a caller reference before draining it so normal Pool shutdown remains valid.
  auto caller = Ref<Arena>::retain(*arena);
  if (reuse) {
    auto neighbor = take(allocate(pool, request(body)));
    check(neighbor.address != original, "pending physical return was reused early");
    fill(neighbor, 67);
    check(worker::poll(lane, 1) == 1, "return Message did not drain");
    auto replacement = take(allocate(pool, request(body)));
    check(replacement.address == original, "drained finite identity or window did not become reusable");
    fill(replacement, 89);
    auto fresh = take(hold(replacement, {0, body + 16}));
    auto adjacent = take(hold(neighbor, {0, body}));
    verify(adjacent, body, 67);
    release(adjacent); release(neighbor); release(replacement);
    caller.reset();
    check(successful_unmaps.load() == 0, "old return retired the replacement's physical pin");
    verify(fresh, body, 89);
    caller = Ref<Arena>::retain(*arena);
    release(fresh);
  } else {
    const auto waiting = close(pool);
    check(waiting.kind() == Progress::Kind::waiting && pool.lane == &lane,
          "close detached the route before final queued return");
  }
  finish_close(pool, lane);
  check(arena->charged_bytes.load() == 0, "closed Pool retained physical or metadata charge");
  check(!worker::pending(lane), "closed Pool retained queue ownership");
  caller.reset();
  check(successful_unmaps.load() == 1, "captured backing was not retired exactly once");
  tracked_address.store(0, std::memory_order_release);
  worker::relinquish(lane);
}

void shared_pool_removal() {
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  const auto small = 2 * page;
  const auto large = std::max(std::size_t{512 * 1024}, 8 * page);
  auto arena = take(open_arena(large + 8 * page));
  worker::Lane lane;
  check(bool(worker::adopt(lane)), "adopt shared-Pool Lane");
  Pool* pools[4];
  Allocation values[4];
  for (unsigned index = 0; index < 4; ++index) {
    pools[index] = take(add_pool(*arena, lane));
    values[index] = take(allocate(*pools[index], {small, 0, 64, 0, Initialization::zero}));
    fill(values[index], 11 + index);
  }
  const auto premature = remove_pool(*pools[1]);
  check(!premature && premature.error() == Error::conflict,
        "removal accepted a live Pool");
  auto remove = [&](unsigned index) {
    release(values[index]);
    finish_close(*pools[index], lane);
    const auto final = statistics(*pools[index]);
    check(final.mapped_bytes == 0 && final.metadata_bytes == 0 && final.reserved_bytes == 0,
          "closed Pool retained charged inventory");
    check(bool(remove_pool(*pools[index])), "remove closed Pool");
    pools[index] = nullptr;
    while (worker::pending(lane)) worker::poll(lane, 32);
  };
  // Four actual Pools allow removing the middle, then head, then tail while
  // another remains live. Refunds must only visit surviving notification routes.
  remove(1);
  remove(3);
  auto& survivor = *pools[2];
  auto& donor = *pools[0];
  auto backing = take(allocate(donor, {large, 0, 64, 0, Initialization::zero}));
  fill(backing, 59);
  auto refused = allocate(survivor, {large, 0, 64});
  check(!refused && refused.error() == Error::capacity,
        "shared budget admitted overlapping large backing");
  release(backing, {Temperature::cold});
  while (worker::pending(lane)) worker::poll(lane, 32);
  bool woken = false;
  WaitLink waiter{&survivor.capacity_changed, survivor.capacity_changed.waiters, &woken,
      [](void* target) noexcept { *static_cast<bool*>(target) = true; }};
  survivor.capacity_changed.waiters = &waiter;
  const auto charged = arena->charged_bytes.load();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto progress = trim_cold(donor, 0);
    check(progress.kind() != Progress::Kind::failed, "donor trim failed");
    if (progress.kind() == Progress::Kind::done && woken) break;
    check(std::chrono::steady_clock::now() < deadline, "surviving Pool missed budget refund");
    worker::poll(lane, 32);
    std::this_thread::yield();
  }
  check(!waiter.source && arena->charged_bytes.load() <= charged - large,
        "wake retained a link or preceded actual backing refund");
  auto admitted = take(allocate(survivor, {large, 0, 64, 0, Initialization::zero}));
  fill(admitted, 73);
  for (const auto index : {0u, 2u}) {
    auto captured = take(hold(values[index], {0, small}));
    verify(captured, small, 11 + index);
  }
  auto captured = take(hold(admitted, {0, large}));
  verify(captured, large, 73);
  release(captured); release(admitted);
  remove(0);
  auto after = take(allocate(survivor, {small, 0, 64, 0, Initialization::zero}));
  fill(after, 97);
  captured = take(hold(after, {0, small}));
  verify(captured, small, 97);
  release(captured); release(after);
  remove(2);
  check(arena->charged_bytes.load() == 0 && !worker::pending(lane),
        "removed shared Pools retained budget or notification ownership");
  arena.reset();
  worker::relinquish(lane);
}

void release_and_growth_overlap(std::size_t body) {
  auto arena = take(open_arena(16 * 1024 * 1024));
  worker::Lane lane;
  check(bool(worker::adopt(lane)), "adopt release/growth Lane");
  auto& pool = *take(add_pool(*arena, lane));
  for (unsigned round = 0; round < 48; ++round) {
    auto value = take(allocate(pool, request(body)));
    fill(value, round);
    auto previous = take(hold(value, {0, body + 16}));
    std::atomic<bool> begin{};
    std::thread foreign([owned = std::move(previous), &begin, body, round]() mutable {
      while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
      verify(owned, body, round);
      release(owned, {.temperature = Temperature::warm,
          .last_access = std::chrono::steady_clock::now()});
    });
    const AllocationRequest larger{body * 5, body * 8, 64, 16, Initialization::zero};
    auto denied = grow(value, larger);
    check(!denied && denied.error() == Error::conflict,
          "growth rewrote a genuinely held prefix/padding");
    begin.store(true, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      auto grown = grow(value, larger);
      if (grown) break;
      check(grown.error() == Error::conflict, "overlapping growth failed for an unrelated reason");
      check(std::chrono::steady_clock::now() < deadline, "growth did not observe final foreign release");
      std::this_thread::yield();
    }
    // Growth can finish before the foreign thread returns from release. Its
    // captured payload must already be dead; old Storage may be reusable now.
    auto current = take(hold(value, {0, value.size + value.readable_padding}));
    verify(current, body, round);
    release(value);
    auto copy_after_owner = retain(current);
    release(current);
    foreign.join();
    verify(copy_after_owner, body, round);
    release(copy_after_owner);
  }
  // The high-water footprint remains conservative while a narrower Hold lives.
  auto value = take(allocate(pool, request(body)));
  fill(value, 23);
  auto widest = take(hold(value, {0, body + 16}));
  auto narrower = take(hold(value, {0, 16}));
  std::thread foreign([owned = std::move(widest)]() mutable { release(owned); });
  foreign.join();
  const AllocationRequest append{body + 8, body * 4, 64, 8, Initialization::zero};
  auto denied = grow(value, append);
  check(!denied && denied.error() == Error::conflict,
        "ending widest Hold prematurely reset the high-water footprint");
  release(narrower);
  auto fresh = take(hold(value, {0, 16}));
  check(bool(grow(value, append)), "fresh capture did not replace an ended footprint cohort");
  verify(fresh, body, 23);
  release(fresh); release(value);
  finish_close(pool, lane);
  check(arena->charged_bytes.load() == 0, "release/growth history retained charges");
  check(bool(remove_pool(pool)), "remove release/growth Pool");
  arena.reset();
  worker::relinquish(lane);
}
} // namespace

extern "C" int __real_munmap(void*, std::size_t);

extern "C" int __wrap_munmap(void* address, std::size_t length) {
  const auto result = __real_munmap(address, length);
  const auto target = tracked_address.load(std::memory_order_acquire);
  const auto begin = reinterpret_cast<std::uintptr_t>(address);
  if (!result && target && target >= begin && target - begin < length)
    successful_unmaps.fetch_add(1, std::memory_order_relaxed);
  return result;
}

int main() {
  for (const auto body : {std::size_t{64}, std::size_t{8192}}) {
    local_last_use(body);
    foreign_last_use(body, false);
    foreign_last_use(body, true);
    release_and_growth_overlap(body);
  }
  shared_pool_removal();
  std::puts("lifetime: sole pins, retained Holds, foreign returns, reuse, Pool removal and budget wakes pass");
}
