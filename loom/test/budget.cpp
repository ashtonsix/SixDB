#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <source_location>
#include <thread>
#include <unistd.h>

namespace {
namespace alloc = orbital::allocation;
using orbital::Error;
using orbital::Progress;
using Clock = std::chrono::steady_clock;
void check(bool good, const char* reason,
    std::source_location where = std::source_location::current()) noexcept {
  if (good) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
  std::abort();
}

#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class T> T take(orbital::Result<T> result) {
  CHECK(result);
  return std::move(*result);
}

template<class Predicate> void await(Predicate ready) {
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!ready()) {
    CHECK(Clock::now() < deadline);
    std::this_thread::yield();
  }
}

// Deadlines terminate liveness failures; they are not measured latency limits.
struct Watchdog {
  std::mutex mutex;
  std::condition_variable changed;
  bool finished{};
  std::thread thread{[this] {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(30), [&] { return finished; })) {
      std::fputs("shared-budget proof failed to return or drain\n", stderr);
      std::abort();
    }
  }};

  ~Watchdog() {
    { std::lock_guard lock(mutex); finished = true; }
    changed.notify_one();
    thread.join();
  }
};

struct Endpoint {
  loom::Worker worker;
  std::atomic<std::uint64_t> posts{};
  std::uint64_t handled{};
  bool adopted{};

  static void posted(void* context) noexcept {
    static_cast<Endpoint*>(context)->posts.fetch_add(1, std::memory_order_release);
  }
};

struct Fixture {
  static constexpr std::size_t body = 1u << 20;
  orbital::Ref<alloc::Arena> arena;
  std::array<Endpoint, 5> endpoints;
  unsigned count;
  alloc::AllocationRequest request{.bytes = body, .reserved_bytes = body,
      .alignment = 64, .initialization = alloc::Initialization::zero};
  bool finished{};

  explicit Fixture(unsigned n, int task_endpoint = -1) : count(n) {
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    CHECK(page > 3072 && count <= endpoints.size());
    arena = take(alloc::open_arena(body + 4 * page));
    CHECK(arena->byte_limit < 2 * body);
    for (unsigned i = 0; i < count; ++i) {
      auto& endpoint = endpoints[i];
      endpoint.worker.lane.notify_posted = Endpoint::posted;
      endpoint.worker.lane.notify_context = &endpoint;
      CHECK(loom::adopt(endpoint.worker));
      endpoint.adopted = true;
      CHECK(loom::configure(endpoint.worker, arena, i == unsigned(task_endpoint) ? 1 : 0));
    }
  }

  alloc::Pool& pool(unsigned i) { CHECK(endpoints[i].worker.memory); return *endpoints[i].worker.memory; }

  void poll(unsigned i) {
    endpoints[i].handled += loom::poll(endpoints[i].worker, {.messages = 256, .tasks = 0});
  }

  void poll_all() { for (unsigned i = 0; i < count; ++i) poll(i); }

  std::uint64_t base_references() const {
    std::uint64_t expected = 1;
    for (unsigned i = 0; i < count; ++i) {
      expected += bool(endpoints[i].worker.arena);
      // A Pool with a live or returning use holds one Arena pin.
      if (endpoints[i].worker.memory) expected += alloc::statistics(*endpoints[i].worker.memory).arena_pins;
    }
    return expected;
  }

  bool routes_settled(std::uint64_t live_allocation_pins = 0) const {
    for (unsigned i = 0; i < count; ++i) {
      const auto& endpoint = endpoints[i];
      if (orbital::worker::pending(endpoint.worker.lane) ||
          endpoint.handled != endpoint.posts.load(std::memory_order_acquire)) return false;
    }
    return arena->references.load() == base_references() + live_allocation_pins;
  }

  bool empty_settled() const { return arena->charged_bytes.load() == 0 && routes_settled(); }

  alloc::Allocation allocate(unsigned i) {
    auto value = take(alloc::allocate(pool(i), request));
    CHECK(arena->charged_bytes.load() >= body && arena->charged_bytes.load() <= arena->byte_limit);
    value.address[0] = std::byte{0x37};
    value.address[body - 1] = std::byte{0x6b};
    return value;
  }

  void refuse(unsigned i, unsigned repeats = 1) {
    const auto charged = arena->charged_bytes.load();
    const auto allocations = alloc::statistics(pool(i)).allocations;
    for (unsigned round = 0; round < repeats; ++round) {
      auto result = alloc::allocate(pool(i), request);
      CHECK(!result && result.error() == Error::capacity);
      CHECK(arena->charged_bytes.load() == charged);
      CHECK(alloc::statistics(pool(i)).allocations == allocations);
    }
  }

  void retire(unsigned i, alloc::Allocation& value, bool all = true) {
    alloc::release(value, {.temperature = alloc::Temperature::cold});
    CHECK(alloc::trim_cold(pool(i), 0).kind() == Progress::Kind::waiting);
    await([&] {
      if (all) poll_all(); else poll(i);
      const auto stats = alloc::statistics(pool(i));
      return stats.mapped_bytes == 0 && stats.metadata_bytes == 0 && arena->charged_bytes.load() == 0 &&
          (!all || empty_settled());
    });
  }

  alloc::Statistics close_one(unsigned i, bool already_queued = false) {
    auto& endpoint = endpoints[i];
    auto& selected = pool(i);
    if (already_queued) {
      CHECK(orbital::worker::pending(endpoint.worker.lane));
      CHECK(alloc::close(selected).kind() == Progress::Kind::waiting);
      CHECK(selected.lane == &endpoint.worker.lane);
      auto premature = alloc::remove_pool(selected);
      CHECK(!premature && premature.error() == Error::conflict);
    }
    await([&] {
      const auto result = alloc::close(selected);
      CHECK(result.kind() != Progress::Kind::failed);
      if (result.kind() == Progress::Kind::done) return true;
      poll(i);
      return false;
    });
    const auto stats = alloc::statistics(selected);
    CHECK(!selected.lane && stats.mapped_bytes == 0 && stats.metadata_bytes == 0 && stats.reserved_bytes == 0);
    await([&] {
      const auto result = loom::shutdown(endpoint.worker);
      CHECK(result.kind() != Progress::Kind::failed);
      if (result.kind() == Progress::Kind::done) return true;
      poll(i);
      return false;
    });
    CHECK(!endpoint.worker.memory && !endpoint.worker.arena);
    return stats;
  }

  void finish() {
    if (finished) return;
    for (unsigned i = 0; i < count; ++i) {
      CHECK(!endpoints[i].worker.active_tasks);
      if (endpoints[i].worker.memory) close_one(i);
    }
    await([&] { poll_all(); return empty_settled(); });
    for (unsigned i = 0; i < count; ++i) {
      loom::relinquish(endpoints[i].worker);
      endpoints[i].adopted = false;
    }
    // Join the Arena's publisher/helper before destroying any wake context.
    arena.reset();
    finished = true;
  }

  ~Fixture() { finish(); }
};

struct DirectWait {
  orbital::WaitLink link;
  unsigned notifications{};

  bool enroll(orbital::Signal& signal, std::uint64_t observed) {
    CHECK(!link.source && !link.next);
    if (signal.generation != observed) return false;
    link = {&signal, signal.waiters, this, [](void* context) noexcept {
      auto& self = *static_cast<DirectWait*>(context);
      CHECK(!self.link.source && !self.link.next);
      ++self.notifications;
    }};
    signal.waiters = &link;
    return true;
  }

  ~DirectWait() { CHECK(!link.source && !link.next); }
};

struct Job {
  loom::Task task;
  loom::Worker& worker;
  alloc::AllocationRequest request;
  alloc::Allocation result;
  unsigned attempts{}, failures{}, successes{};

  explicit Job(loom::Worker& owner, alloc::AllocationRequest r) : worker(owner), request(r) {
    task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& self = *static_cast<Job*>(task.context);
      auto& signal = self.worker.memory->capacity_changed;
      const auto observed = signal.generation;
      ++self.attempts;
      auto value = loom::allocate(self.worker, self.request);
      if (!value) {
        if (value.error() != Error::capacity) return Progress::fail(value.error());
        ++self.failures;
        return Progress::wait(signal, observed);
      }
      self.result = std::move(*value);
      CHECK(self.result.address[0] == std::byte{0} && self.result.address[self.result.size - 1] == std::byte{0});
      self.result.address[0] = std::byte{0x53};
      self.result.address[self.result.size - 1] = std::byte{0x7d};
      ++self.successes;
      return Progress::done();
    };
  }

  ~Job() { CHECK(!task.worker && !task.next && !task.wait.source && !result); }
};

void no_demand(bool baseline = false) {
  Fixture fixture(5);
  for (unsigned round = 0; round < 3; ++round) {
    auto value = fixture.allocate(0);
    fixture.retire(0, value);
    for (unsigned i = 1; i < fixture.count; ++i) {
      if (baseline) {
        CHECK(fixture.endpoints[i].posts.load() > 0 && fixture.endpoints[i].handled > 0);
        CHECK(fixture.pool(i).capacity_changed.generation > 0);
      } else {
        CHECK(fixture.endpoints[i].posts.load() == 0 && fixture.endpoints[i].handled == 0);
        CHECK(fixture.pool(i).capacity_changed.generation == 0);
      }
      CHECK(!fixture.pool(i).capacity_changed.waiters);
    }
  }
  std::uint64_t posts{}, messages{}, pulses{};
  for (unsigned i = 1; i < fixture.count; ++i) {
    posts += fixture.endpoints[i].posts.load();
    messages += fixture.endpoints[i].handled;
    pulses += fixture.pool(i).capacity_changed.generation;
  }
  fixture.finish();
  std::printf("case=no-demand-refund implementation=%s cycles=3 uninterested_peers=4 peer_messages=%llu wake_callbacks=%llu signal_pulses=%llu verified=1 performance_claim=0\n",
      baseline ? "prior-broadcast" : "interest-candidate", static_cast<unsigned long long>(messages),
      static_cast<unsigned long long>(posts), static_cast<unsigned long long>(pulses));
}

void direct_enrollment(bool refund_first) {
  Fixture fixture(3);
  auto donor = fixture.allocate(0);
  auto& signal = fixture.pool(1).capacity_changed;
  const auto observed = signal.generation;
  fixture.refuse(1, 7);
  CHECK(!signal.waiters && fixture.endpoints[1].posts.load() == 0);
  DirectWait waiter;
  if (!refund_first) CHECK(waiter.enroll(signal, observed));
  fixture.retire(0, donor);
  CHECK(fixture.endpoints[1].posts.load() == 1 && signal.generation > observed);
  if (refund_first) {
    CHECK(!waiter.enroll(signal, observed) && waiter.notifications == 0);
  } else CHECK(waiter.notifications == 1 && !signal.waiters);
  auto granted = fixture.allocate(1);
  fixture.refuse(0); // A second full backing remains impossible after the wake.
  fixture.retire(1, granted);
  CHECK(fixture.endpoints[2].posts.load() == 0);
  fixture.finish();
  std::printf("case=direct-refusal refund_%s_enrollment=1 repeated_refusals=7 first_refund_notice_posts=1 verified=1\n",
      refund_first ? "before" : "after");
}

void abandoned_task() {
  Fixture fixture(3, 1);
  auto donor = fixture.allocate(0);
  auto& receiver = fixture.endpoints[1].worker;
  {
    Job job(receiver, fixture.request);
    CHECK(loom::start(receiver, job.task));
    CHECK(loom::poll(receiver, {.messages = 0, .tasks = 1}) == 1);
    CHECK(job.attempts == 1 && job.failures == 1 && job.task.state == loom::TaskState::waiting);
    fixture.refuse(1, 7);
    loom::detach(job.task);
    CHECK(job.task.state == loom::TaskState::detached && !receiver.memory->capacity_changed.waiters);
  } // The client frame is destroyed before the shared-budget notice arrives.
  fixture.retire(0, donor);
  CHECK(fixture.endpoints[1].posts.load() == 1 && !receiver.first && !receiver.active_tasks);
  const auto generation = fixture.pool(1).capacity_changed.generation;
  donor = fixture.allocate(0);
  fixture.retire(0, donor);
  CHECK(fixture.endpoints[1].posts.load() == 1 && fixture.pool(1).capacity_changed.generation == generation);
  CHECK(fixture.endpoints[2].posts.load() == 0);
  fixture.finish();
  std::puts("case=abandoned-task frame_destroyed_before_notice=1 first_notice=1 later_refund_notices=0 verified=1");
}

void abandoned_direct() {
  Fixture fixture(3);
  auto donor = fixture.allocate(0);
  fixture.refuse(1, 7);
  CHECK(!fixture.pool(1).capacity_changed.waiters);
  fixture.retire(0, donor);
  CHECK(fixture.endpoints[1].posts.load() == 1);
  const auto generation = fixture.pool(1).capacity_changed.generation;
  for (unsigned round = 0; round < 2; ++round) {
    donor = fixture.allocate(0);
    fixture.retire(0, donor);
    CHECK(fixture.endpoints[1].posts.load() == 1 && fixture.pool(1).capacity_changed.generation == generation);
  }
  CHECK(fixture.endpoints[2].posts.load() == 0);
  fixture.finish();
  std::puts("case=abandoned-direct repeated_refusals=7 first_notice=1 later_refunds=2 later_notices=0 verified=1");
}

void smaller_claim_preserves_demand() {
  Fixture fixture(3);
  auto donor = fixture.allocate(0);
  const auto donor_charge = fixture.arena->charged_bytes.load();
  fixture.refuse(1);
  auto& signal = fixture.pool(1).capacity_changed;
  DirectWait waiter;
  CHECK(waiter.enroll(signal, signal.generation));
  auto small = take(alloc::allocate(fixture.pool(1), {8192, 8192, 64}));
  const auto small_charge = fixture.arena->charged_bytes.load() - donor_charge;
  CHECK(small_charge >= 8192 && small_charge < Fixture::body);
  small.address[0] = std::byte{0x28}; small.address[small.size - 1] = std::byte{0x61};
  alloc::release(donor, {.temperature = alloc::Temperature::cold});
  CHECK(alloc::trim_cold(fixture.pool(0), 0).kind() == Progress::Kind::waiting);
  await([&] {
    fixture.poll_all();
    const auto stats = alloc::statistics(fixture.pool(0));
    return stats.mapped_bytes == 0 && stats.metadata_bytes == 0 &&
        fixture.arena->charged_bytes.load() == small_charge && waiter.notifications == 1 && fixture.routes_settled();
  });
  CHECK(fixture.endpoints[1].posts.load() == 1 && !signal.waiters);
  CHECK(small.address[0] == std::byte{0x28} && small.address[small.size - 1] == std::byte{0x61});
  fixture.retire(1, small);
  // Each local Window retirement contributes its own VM reply Message.
  // The first selected capacity notice above is the only refusal notice.
  CHECK(fixture.endpoints[1].posts.load() == 2);
  auto grant = fixture.allocate(1);
  fixture.retire(1, grant);
  CHECK(fixture.endpoints[1].posts.load() == 3 && fixture.endpoints[2].posts.load() == 0);
  fixture.finish();
  std::puts("case=successful-smaller-claim prior_refused_demand_preserved=1 selected_notices=1 live_backing_preserved=1 verified=1");
}

void queued_removal() {
  Fixture fixture(5);
  auto donor = fixture.allocate(4);
  for (unsigned i = 0; i < 4; ++i) fixture.refuse(i);
  fixture.retire(4, donor, false);
  await([&] {
    fixture.poll(4);
    for (unsigned i = 0; i < 4; ++i)
      if (fixture.endpoints[i].posts.load(std::memory_order_acquire) != 1 ||
          !orbital::worker::pending(fixture.endpoints[i].worker.lane)) return false;
    // Each of the four queued owner notices retains one Arena pin. No donor
    // pin/job/reply remains once its lane balances and reaches this count.
    return fixture.arena->references.load() == fixture.base_references() + 4 &&
        !orbital::worker::pending(fixture.endpoints[4].worker.lane) &&
        fixture.endpoints[4].handled == fixture.endpoints[4].posts.load(std::memory_order_acquire);
  });
  donor = fixture.allocate(4);
  // Register a fresh interest wave while the first wave's notices remain queued.
  for (unsigned i = 0; i < 4; ++i) fixture.refuse(i, 3);
  // Refusal order 0,1,2,3 inserts head 3, middle 1, tail 0 in the candidate.
  // Every selected Pool also has its first-wave retained notice outstanding.
  std::array<std::uint64_t, 3> closed_posts;
  const std::array<unsigned, 3> order{3, 1, 0};
  for (unsigned j = 0; j < order.size(); ++j) {
    fixture.close_one(order[j], true);
    closed_posts[j] = fixture.endpoints[order[j]].posts.load();
  }
  auto& survivor = fixture.pool(2).capacity_changed;
  DirectWait waiter;
  CHECK(waiter.enroll(survivor, survivor.generation));
  fixture.retire(4, donor);
  CHECK(waiter.notifications == 1 && !survivor.waiters);
  for (unsigned j = 0; j < order.size(); ++j)
    CHECK(fixture.endpoints[order[j]].posts.load() == closed_posts[j]);
  auto granted = fixture.allocate(2);
  fixture.retire(2, granted);
  fixture.finish();
  std::puts("case=queued-interest-removal head=3 middle=1 tail=0 closed_routes_revisited=0 survivor_admitted=1 verified=1");
}

struct Marker {
  orbital::worker::Message message;
  unsigned handled{}, released{};
  static const orbital::worker::MessageOps ops;

  Marker() { message.context = this; message.ops = &ops; }
};

const orbital::worker::MessageOps Marker::ops{
    [](orbital::worker::Message&) noexcept {},
    [](orbital::worker::Message& message, orbital::worker::Lane&) noexcept {
      ++static_cast<Marker*>(message.context)->handled;
    },
    [](orbital::worker::Message& message) noexcept {
      ++static_cast<Marker*>(message.context)->released;
    }};

void foreign_return() {
  Fixture fixture(3, 1);
  auto donor = fixture.allocate(0);
  auto held = take(alloc::hold(donor, {0, donor.size}));
  alloc::release(donor);
  const auto charged = fixture.arena->charged_bytes.load();
  auto& receiver = fixture.endpoints[1].worker;
  Job job(receiver, fixture.request);
  CHECK(loom::start(receiver, job.task));
  CHECK(loom::poll(receiver, {.messages = 0, .tasks = 1}) == 1);
  CHECK(job.task.state == loom::TaskState::waiting && job.attempts == 1);
  std::atomic<bool> entered{}, go{};
  std::thread foreign([hold = std::move(held), &entered, &go]() mutable {
    CHECK(hold.address[0] == std::byte{0x37} && hold.address[hold.footprint.size - 1] == std::byte{0x6b});
    entered.store(true, std::memory_order_release);
    go.wait(false, std::memory_order_acquire);
    alloc::release(hold);
  });
  await([&] { return entered.load(std::memory_order_acquire); });
  CHECK(alloc::close(fixture.pool(0)).kind() == Progress::Kind::waiting);
  CHECK(fixture.arena->charged_bytes.load() == charged);
  Marker marker;
  orbital::worker::post(receiver.lane, marker.message);
  fixture.poll(1);
  CHECK(marker.handled == 1 && marker.released == 1 && !go.load());
  CHECK(job.attempts == 1 && job.task.state == loom::TaskState::waiting);
  CHECK(fixture.endpoints[2].posts.load() == 0);
  go.store(true, std::memory_order_release); go.notify_one();
  foreign.join();
  CHECK(orbital::worker::pending(fixture.endpoints[0].worker.lane));
  CHECK(fixture.arena->charged_bytes.load() == charged && alloc::statistics(fixture.pool(0)).foreign_returns == 0);
  const auto stats = fixture.close_one(0);
  CHECK(stats.foreign_returns == 1 && fixture.arena->charged_bytes.load() == 0);
  await([&] { fixture.poll_all(); return job.task.state == loom::TaskState::queued; });
  CHECK(job.attempts == 1 && !job.task.wait.source && !job.result);
  CHECK(loom::poll(receiver, {.messages = 0, .tasks = 1}) == 1);
  CHECK(job.task.state == loom::TaskState::done && job.attempts == 2 && job.failures == 1 && job.successes == 1);
  CHECK(job.result.address[0] == std::byte{0x53} && job.result.address[job.result.size - 1] == std::byte{0x7d});
  CHECK(marker.handled == 1 && marker.released == 1 && fixture.endpoints[2].posts.load() == 0);
  fixture.retire(1, job.result);
  fixture.finish();
  std::puts("case=foreign-final-hold owner_progress_before_release=1 no_task_inline=1 physical_refund_after_owner_return=1 admitted_once=1 verified=1");
}
} // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  Watchdog watchdog;
  if (argc == 2 && std::strcmp(argv[1], "--baseline-no-demand") == 0) {
    no_demand(true);
    return 0;
  }
  CHECK(argc == 1);
  no_demand();
  direct_enrollment(true);
  direct_enrollment(false);
  abandoned_direct();
  abandoned_task();
  smaller_claim_preserves_demand();
  queued_removal();
  foreign_return();
  std::puts("summary=private-shared-budget-interest-proofs-passed performance_claim=0");
}
