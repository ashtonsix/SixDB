#include <orbital/allocation.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

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

template<class T> T take(Result<T> result) {
  check(result.has_value(), "operation rejected");
  return std::move(*result);
}

template<class Predicate> void await(Predicate ready, const char* reason) {
  const auto deadline = Clock::now() + timeout;
  while (!ready()) {
    check(Clock::now() < deadline, reason);
    std::this_thread::yield();
  }
}

// Only the chosen foreign thread arms this hook, immediately before its final
// release. The actual mutex stays held; atomics avoid recursive hook locking.
thread_local bool pause_after_lock{};
thread_local bool fail_next_mapping{};
std::atomic<bool> paused{}, resume_publisher{};

struct Completion {
  unsigned handled{}, released{};
  bool during_pause{};
};

void retain_message(worker::Message&) noexcept {}

void handle_message(worker::Message& message, worker::Lane&) noexcept {
  auto& completion = *static_cast<Completion*>(message.context);
  ++completion.handled;
  completion.during_pause = paused.load(std::memory_order_acquire) &&
      !resume_publisher.load(std::memory_order_acquire);
}

void release_message(worker::Message& message) noexcept {
  ++static_cast<Completion*>(message.context)->released;
}

const worker::MessageOps completion_ops{retain_message, handle_message, release_message};

void paused_final_publication() {
  auto arena = take(open_arena(16 * 1024 * 1024));
  worker::Lane lane;
  check(worker::adopt(lane).has_value(), "caller could not adopt Lane");
  auto& pool = *take(add_pool(*arena, lane));
  auto first = take(allocate(pool, {64, 0, 16}));
  auto second = take(allocate(pool, {64, 0, 16}));
  auto neighbor = take(allocate(pool, {64, 0, 16}));
  std::memset(neighbor.address, 0x6b, neighbor.size);
  auto earlier = take(hold(first, {0, 64}));
  auto later = take(hold(second, {0, 64}));
  release(first); release(second);
  const auto charged = arena->charged_bytes.load();

  std::thread prior([held = std::move(earlier)]() mutable { release(held); });
  prior.join();
  check(worker::pending(lane), "setup did not queue the earlier foreign return");
  check(statistics(pool).foreign_returns == 0, "setup consumed the earlier return");
  std::thread publisher([held = std::move(later)]() mutable {
    pause_after_lock = true;
    release(held);
    check(!pause_after_lock, "final foreign release never acquired its mutex");
  });
  await([] { return paused.load(std::memory_order_acquire); }, "publisher did not acquire its actual mutex");

  Completion completion;
  worker::Message unrelated{nullptr, &completion, &completion_ops};
  worker::post(lane, unrelated);
  std::atomic<bool> poll_finished{}, rescued{};
  // An old blocking consumer needs rescuing so it can fail cleanly. Successful
  // polling is determined by the held-mutex history, not a latency threshold.
  std::thread watchdog([&] {
    const auto deadline = Clock::now() + timeout;
    while (!poll_finished.load(std::memory_order_acquire)) {
      if (Clock::now() >= deadline) {
        rescued.store(true, std::memory_order_release);
        resume_publisher.store(true, std::memory_order_release);
        return;
      }
      std::this_thread::yield();
    }
  });
  const auto processed = worker::poll(lane, 2);
  poll_finished.store(true, std::memory_order_release);
  watchdog.join();
  check(!rescued.load(std::memory_order_acquire), "owner polling blocked behind the paused foreign publisher");
  check(!resume_publisher.load(std::memory_order_acquire), "publisher resumed before owner polling returned");
  check(processed == 2 && completion.handled == 1 && completion.released == 1,
      "busy allocation return prevented unrelated completion");
  check(statistics(pool).foreign_returns == 0, "owner consumed an incompletely published return");
  check(arena->charged_bytes.load() == charged, "pending return lost its backing charge");
  for (auto byte : bytes(neighbor)) check(byte == std::byte{0x6b}, "pending return damaged live neighbors");

  resume_publisher.store(true, std::memory_order_release);
  publisher.join();
  await([&] { worker::poll(lane, 16); return !worker::pending(lane); }, "deferred returns did not drain");
  const auto after = statistics(pool);
  check(after.foreign_returns == 2, "foreign returns were lost or consumed twice");
  auto reused_first = take(allocate(pool, {64, 0, 16}));
  auto reused_second = take(allocate(pool, {64, 0, 16}));
  check(statistics(pool).reuses == after.reuses + 2 && arena->charged_bytes.load() == charged,
      "drained identities did not return to prepared local inventory");
  check(reused_first.address != reused_second.address && reused_first.address != neighbor.address &&
      reused_second.address != neighbor.address, "return recycling aliased live homes");
  std::memset(reused_first.address, 0x39, reused_first.size);
  std::memset(reused_second.address, 0x4d, reused_second.size);
  for (auto byte : bytes(neighbor)) check(byte == std::byte{0x6b}, "reused return overlapped neighboring data");
  for (auto byte : bytes(reused_first)) check(byte == std::byte{0x39}, "reused homes overlapped each other");
  release(reused_first); release(reused_second); release(neighbor);
  await([&] {
    const auto result = close(pool);
    check(result.kind() != Progress::Kind::failed, "Pool close failed after deferred returns");
    if (result.kind() == Progress::Kind::done) return true;
    worker::poll(lane, 16);
    return false;
  }, "Pool could not close after deferred returns");
  check(arena->charged_bytes.load() == 0 && !worker::pending(lane), "closed Pool retained backing or return Messages");
  check(statistics(pool).foreign_returns == 2 && completion.handled == 1 && completion.released == 1,
      "close replayed a consumed return or completion");
  worker::relinquish(lane);
}

void paused_registry(bool physical_completion) {
  paused.store(false); resume_publisher.store(false);
  const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  auto arena = take(open_arena((1u << 20) + 4 * page));
  worker::Lane lane;
  check(worker::adopt(lane).has_value(), "adopt registry test Lane");
  auto& pool = *take(add_pool(*arena, lane));
  // This physical occupancy makes a large request legal in an empty Arena,
  // but impossible in the current shared byte budget. It is not an oversized
  // request that could never be admitted after a refund.
  auto occupied = take(allocate(pool, {8192, 8192, 64}));
  const auto occupied_charge = arena->charged_bytes.load();
  const auto occupied_mapped = statistics(pool).mapped_bytes;
  const AllocationRequest demand{arena->byte_limit - page, 0, 64};
  if (physical_completion) {
    auto memory = take(allocate(pool, {8192, 8192, 64}));
    release(memory);
    const auto progress = trim_cold(pool, 0);
    check(progress.kind() == Progress::Kind::waiting, "real trim did not start");
    await([&] { return worker::pending(lane); }, "real VM completion never arrived");
    check(arena->charged_bytes.load() > occupied_charge,
        "VM work refunded before its owner completion");
  } else {
    fail_next_mapping = true;
    const auto denied = allocate(pool, {8192, 8192, 64});
    check(!denied && denied.error() == Error::io && !fail_next_mapping,
        "failed mapping did not execute the actual rollback/refund");
    check(arena->charged_bytes.load() == occupied_charge && !worker::pending(lane),
        "no-demand rollback changed occupied charge or queued a notice");
  }

  std::atomic<bool> peer_added{}, clean_peer{};
  std::thread peer([&] {
    worker::Lane peer_lane;
    check(worker::adopt(peer_lane).has_value(), "adopt registry holder Lane");
    // Retained wrapper: add_pool's actual registry mutex is its sole lock
    // after record setup. No allocator source hook is installed.
    pause_after_lock = true;
    auto& peer_pool = *take(add_pool(*arena, peer_lane));
    check(!pause_after_lock, "peer did not enter the actual registry mutex");
    peer_added.store(true, std::memory_order_release);
    await([&] { return clean_peer.load(std::memory_order_acquire); }, "peer cleanup not requested");
    await([&] {
      const auto result = close(peer_pool);
      check(result.kind() != Progress::Kind::failed, "registry peer close failed");
      if (result.kind() == Progress::Kind::done) return true;
      worker::poll(peer_lane, 16);
      return false;
    }, "registry peer close did not drain");
    check(remove_pool(peer_pool).has_value() && !worker::pending(peer_lane),
        "registry peer retained notification routing after removal");
    worker::relinquish(peer_lane);
  });
  await([] { return paused.load(std::memory_order_acquire); }, "peer never held the registry");
  std::atomic<bool> finished{}, rescued{};
  std::thread watchdog([&] {
    const auto deadline = Clock::now() + timeout;
    while (!finished.load(std::memory_order_acquire)) {
      if (Clock::now() >= deadline) {
        rescued.store(true, std::memory_order_release);
        resume_publisher.store(true, std::memory_order_release);
        return;
      }
      std::this_thread::yield();
    }
  });

  const auto before_refusal = arena->charged_bytes.load();
  const auto capacity = allocate(pool, demand);
  check(!capacity && capacity.error() == Error::capacity &&
      arena->charged_bytes.load() == before_refusal && worker::pending(lane),
      "genuine contended budget refusal did not preserve charge and queue local retry");
  Completion completion;
  worker::Message unrelated{nullptr, &completion, &completion_ops};
  worker::post(lane, unrelated);
  // Physical case: VM reply, contended-refusal retry, then unrelated Message.
  // Rollback case: contended-refusal retry, then unrelated Message.
  const auto processed = worker::poll(lane, physical_completion ? 3 : 2);
  for (unsigned round = 0; round < 8; ++round) {
    const auto refused = allocate(pool, demand);
    check(!refused && refused.error() == Error::capacity,
        "repeat genuine refusal changed its result under held registry");
    fail_next_mapping = true;
    const auto denied = allocate(pool, {8192, 8192, 64});
    check(!denied && denied.error() == Error::io && !fail_next_mapping,
        "later refund did not execute actual mapping rollback");
    check(arena->charged_bytes.load() == occupied_charge,
        "contended retry/rollback changed conserved occupied charge");
  }
  release(occupied);
  const auto closing = close(pool);
  const auto refused = allocate(pool, {64, 0, 16});
  const bool stayed_paused = !resume_publisher.load(std::memory_order_acquire);
  finished.store(true, std::memory_order_release);
  watchdog.join();
  const auto refunded = arena->charged_bytes.load();
  const auto mapped = statistics(pool).mapped_bytes;

  resume_publisher.store(true, std::memory_order_release);
  await([&] { return peer_added.load(std::memory_order_acquire); }, "registry holder did not resume");
  clean_peer.store(true, std::memory_order_release);
  peer.join();
  await([&] {
    const auto result = close(pool);
    check(result.kind() != Progress::Kind::failed, "notice owner close failed");
    if (result.kind() == Progress::Kind::done) return true;
    worker::poll(lane, 16);
    return false;
  }, "deferred registry work did not drain through close");
  check(arena->charged_bytes.load() == 0 && !worker::pending(lane),
      "deferred notices retained charge or completion ownership");
  check(remove_pool(pool).has_value(), "remove closed notice owner");
  worker::relinquish(lane);
  check(!rescued.load(std::memory_order_acquire) && stayed_paused,
      "claim-refusal registration, poll or close blocked behind registry holder");
  check(processed == (physical_completion ? 3u : 2u) && completion.handled == 1 &&
      completion.released == 1 && completion.during_pause,
      "registry contention prevented or repeated unrelated completion");
  check(closing.kind() == Progress::Kind::waiting && !refused && refused.error() == Error::unavailable,
      "contended close failed to preserve readiness or stop new allocations");
  check(refunded == occupied_charge && mapped == occupied_mapped,
      "owner completion/rollback failed to conserve the remaining physical backing");
}
} // namespace

extern "C" void* __real_mmap(void*, std::size_t, int, int, int, off_t) noexcept;
extern "C" void* __wrap_mmap(void* address, std::size_t length, int rights,
    int flags, int descriptor, off_t offset) noexcept {
  if (fail_next_mapping) {
    fail_next_mapping = false;
    errno = ENOMEM;
    return MAP_FAILED;
  }
  return __real_mmap(address, length, rights, flags, descriptor, offset);
}

extern "C" int __real_pthread_mutex_lock(pthread_mutex_t*) noexcept;

extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t* mutex) noexcept {
  const auto result = __real_pthread_mutex_lock(mutex);
  if (!result && pause_after_lock) {
    pause_after_lock = false;
    paused.store(true, std::memory_order_release);
    while (!resume_publisher.load(std::memory_order_acquire)) std::this_thread::yield();
  }
  return result;
}

int main() {
  paused_final_publication();
  paused_registry(false);
  paused_registry(true);
  std::puts("paused foreign/registry publication preserves owner progress, accounting and exact retirement");
}
