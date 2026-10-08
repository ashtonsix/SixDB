#include <orbital/allocation.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <source_location>
#include <thread>

namespace {
using namespace orbital;

void check(bool condition, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
    std::abort();
  }
}

bool pinned_to(int cpu) noexcept {
  cpu_set_t actual;
  return ::sched_getaffinity(0, sizeof(actual), &actual) == 0 &&
      CPU_COUNT(&actual) == 1 && CPU_ISSET(cpu, &actual) && ::sched_getcpu() == cpu;
}

struct Retirement {
  std::atomic<void*> address{};
  std::atomic<unsigned> calls{};
  std::atomic<bool> pinned{}, foreign{};
  int cpu{};
  std::thread::id caller;
} retirement;

void count_callback(void* context) noexcept {
  static_cast<std::atomic<unsigned>*>(context)->fetch_add(1, std::memory_order_relaxed);
}

void rejected_start(int cpu, Error expected) {
  worker::Thread thread;
  std::atomic<unsigned> calls{};
  auto result = worker::spawn(thread, count_callback, &calls, cpu);
  const auto unexpectedly_joinable = thread.native.joinable();
  if (unexpectedly_joinable) check(worker::join(thread).has_value(), "could not clean up rejected start");
  check(!result && result.error() == expected, "thread start did not reject the requested CPU");
  check(!unexpectedly_joinable, "rejected start left a joinable thread");
  check(calls.load() == 0, "rejected start executed its callback");
}

void rejected_arena(int cpu, Error expected) {
  auto result = allocation::open_arena(1 << 20, cpu);
  check(!result && result.error() == expected, "Arena did not reject helper CPU at startup");
}

int unavailable_cpu(const cpu_set_t& allowed) {
  int found = -1;
  // A missing bit in the initial affinity mask is not proof of cpuset refusal.
  // Probe on a disposable real thread so the caller's mask stays unchanged.
  std::thread probe([&] {
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (CPU_ISSET(cpu, &allowed)) continue;
      cpu_set_t requested;
      CPU_ZERO(&requested); CPU_SET(cpu, &requested);
      if (::sched_setaffinity(0, sizeof(requested), &requested) != 0) {
        found = cpu;
        return;
      }
    }
  });
  probe.join();
  return found;
}

struct Nested {
  int parent_cpu{}, child_cpu{};
  std::thread::id caller;
  bool parent_pinned{}, parent_still_pinned{}, child_pinned{};
  bool parent_is_foreign{}, accepted{}, joined{}, rejected_joinable{};
  unsigned child_calls{};
};

void child_callback(void* context) noexcept {
  auto& nested = *static_cast<Nested*>(context);
  // This is the first user work after startup. The kernel must already have
  // accepted the selected CPU; inherited parent affinity is not permission.
  nested.child_pinned = pinned_to(nested.child_cpu);
  ++nested.child_calls;
}

void parent_callback(void* context) noexcept {
  auto& nested = *static_cast<Nested*>(context);
  nested.parent_pinned = pinned_to(nested.parent_cpu);
  nested.parent_is_foreign = std::this_thread::get_id() != nested.caller;
  worker::Thread child;
  auto result = worker::spawn(child, child_callback, &nested, nested.child_cpu);
  nested.accepted = result.has_value();
  // Inspect Thread only after spawn has completed its native assignment.
  if (result) nested.joined = worker::join(child).has_value();
  else if (child.native.joinable()) {
    nested.rejected_joinable = true;
    nested.joined = worker::join(child).has_value();
  }
  nested.parent_still_pinned = pinned_to(nested.parent_cpu);
}

void nested_affinity(int first, int second) {
  Nested nested;
  nested.parent_cpu = first;
  nested.child_cpu = second;
  nested.caller = std::this_thread::get_id();
  worker::Thread parent;
  auto result = worker::spawn(parent, parent_callback, &nested, first);
  check(result.has_value(), "allowed parent CPU was rejected");
  check(worker::join(parent).has_value(), "could not join pinned parent");
  check(!parent.native.joinable(), "joined parent remained joinable");
  check(nested.parent_pinned && nested.parent_still_pinned && nested.parent_is_foreign,
      "parent callback did not run with its requested affinity");
  check(!nested.rejected_joinable, "rejected nested start left a joinable thread");
  check(nested.accepted, "pinned parent's inherited mask incorrectly rejected an allowed child CPU");
  check(nested.joined && nested.child_calls == 1 && nested.child_pinned,
      "nested callback ran before successful affinity setup or failed to retire");
}

struct ArenaRun { int caller_cpu{}, helper_cpu{}; };

void arena_retirement(void* context) noexcept {
  const auto& run = *static_cast<ArenaRun*>(context);
  check(pinned_to(run.caller_cpu), "Arena caller lost its assigned CPU");
  {
    auto opened = allocation::open_arena(1 << 20, run.helper_cpu);
    check(opened.has_value(), "Arena helper startup rejected allowed CPU");
    auto arena = std::move(*opened);
    worker::Lane lane;
    check(worker::adopt(lane).has_value(), "could not adopt Arena caller lane");
    auto added = allocation::add_pool(*arena, lane);
    check(added.has_value(), "could not add retirement Pool");
    auto& pool = **added;
    auto value = allocation::allocate(pool, {.bytes = 8192, .alignment = 64});
    check(value.has_value(), "could not allocate retirement Window");
    retirement.cpu = run.helper_cpu;
    retirement.caller = std::this_thread::get_id();
    retirement.calls = 0; retirement.pinned = false; retirement.foreign = false;
    retirement.address.store(value->address, std::memory_order_release);
    allocation::release(*value);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      const auto progress = allocation::close(pool);
      if (progress.kind() == Progress::Kind::done) break;
      check(progress.kind() != Progress::Kind::failed, "retirement failed");
      check(std::chrono::steady_clock::now() < deadline, "retirement failed to drain");
      worker::poll(lane, 16);
      std::this_thread::yield();
    }
    check(retirement.calls.load(std::memory_order_acquire) == 1 &&
        retirement.pinned.load() && retirement.foreign.load(),
        "actual Window retirement did not run on the selected helper CPU");
    check(arena->charged_bytes.load() == 0, "retirement retained its budget charge");
    retirement.address.store(nullptr, std::memory_order_release);
    check(allocation::remove_pool(pool).has_value(), "could not remove closed retirement Pool");
    worker::relinquish(lane);
  } // Join the actual helper before its caller frame ends.
  check(pinned_to(run.caller_cpu), "helper placement changed caller affinity");
}

void arena_affinity(int first, int second) {
  ArenaRun run{first, second};
  worker::Thread caller;
  check(worker::spawn(caller, arena_retirement, &run, first).has_value(),
      "could not start assigned Arena caller");
  check(worker::join(caller).has_value(), "could not join Arena caller after helper retirement");
}
} // namespace

extern "C" int __real_munmap(void*, std::size_t) noexcept;

extern "C" int __wrap_munmap(void* address, std::size_t bytes) noexcept {
  if (address && address == retirement.address.load(std::memory_order_acquire)) {
    retirement.pinned.store(pinned_to(retirement.cpu), std::memory_order_relaxed);
    retirement.foreign.store(std::this_thread::get_id() != retirement.caller, std::memory_order_relaxed);
    retirement.calls.fetch_add(1, std::memory_order_release);
  }
  return __real_munmap(address, bytes);
}

int main() {
  cpu_set_t allowed;
  check(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0, "could not inspect actual allowed CPU inventory");
  int first = -1, second = -1;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &allowed)) continue;
    if (first < 0) first = cpu;
    else if (second < 0) second = cpu;
  }
  check(first >= 0, "process has no allowed CPU");
  rejected_start(-2, Error::invalid_argument);
  rejected_start(CPU_SETSIZE, Error::invalid_argument);
  rejected_arena(-2, Error::invalid_argument);
  rejected_arena(CPU_SETSIZE, Error::invalid_argument);
  const auto unavailable = unavailable_cpu(allowed);
  if (unavailable >= 0) {
    rejected_start(unavailable, Error::unavailable);
    rejected_arena(unavailable, Error::unavailable);
  }
  else std::puts("skip unavailable-CPU startup case: full fixed-size CPU mask allowed");
  if (second < 0) {
    std::puts("skip cross-CPU nested spawn: only one CPU available; exercising same-CPU startup");
    nested_affinity(first, first);
  } else nested_affinity(first, second);
  arena_affinity(first, second < 0 ? first : second);
  std::puts("nested thread/helper affinity, startup readiness, retirement and rejected-start cleanup pass");
}
