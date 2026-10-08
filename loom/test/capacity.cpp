#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <source_location>
#include <thread>
#include <vector>
#include <unistd.h>

// Poll budgets count Message dispatches and Task steps, not callbacks inside
// Signal::pulse. With no Task visits during release/return handling, each
// waiting -> queued transition counts one actual notification. The one-credit
// waves below deliberately retain the winner until all other retries repark;
// their triangular logical totals do not estimate general workload cost.
namespace {
namespace alloc = orbital::allocation;
using orbital::Error;
using orbital::Progress;
using loom::TaskState;
std::size_t checks{};

void check(bool valid, const char* expression,
    std::source_location where = std::source_location::current()) {
  ++checks;
  if (valid) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}

#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class T> T required(orbital::Result<T> value) {
  CHECK(value);
  return std::move(*value);
}

// A liveness failure must terminate, including a stuck foreign return publisher.
// This is a correctness deadline, not a latency assertion or timed measurement.
struct Watchdog {
  std::mutex mutex;
  std::condition_variable changed;
  bool done{};
  std::thread thread{[this] {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(30), [this] { return done; })) {
      std::fputs("capacity check failed to drain within watchdog deadline\n", stderr);
      std::abort();
    }
  }};

  ~Watchdog() {
    { std::lock_guard lock(mutex); done = true; }
    changed.notify_one(); thread.join();
  }
};

struct Fixture {
  std::size_t page;
  orbital::Ref<alloc::Arena> arena;
  loom::Worker worker;
  std::vector<alloc::Allocation> occupied;
  alloc::AllocationRequest request;
  bool finished{};

  explicit Fixture(std::size_t task_capacity)
      : page(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))),
        arena(required(alloc::open_arena(page * 40))),
        request{.bytes = page, .reserved_bytes = page, .alignment = page,
                .initialization = alloc::Initialization::zero} {
    CHECK(page > 3072 && page <= (1u << 20));
    CHECK(loom::adopt(worker));
    CHECK(loom::configure(worker, arena, task_capacity));
    occupied.reserve(64);
    bool saturated = false;
    for (unsigned i = 0; i < 256; ++i) {
      auto storage = loom::allocate(worker, request);
      if (!storage) { CHECK(storage.error() == Error::capacity); saturated = true; break; }
      occupied.push_back(std::move(*storage));
    }
    CHECK(saturated && !occupied.empty());
    CHECK(!orbital::worker::pending(worker.lane));
  }

  alloc::Allocation take_one() {
    CHECK(!occupied.empty());
    auto result = std::move(occupied.back()); occupied.pop_back();
    return result;
  }

  void finish() {
    if (finished) return;
    CHECK(!worker.active_tasks && !worker.first && !worker.last);
    occupied.clear();
    bool drained = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != Progress::Kind::failed);
      if (progress.kind() == Progress::Kind::done) { drained = true; break; }
      if (!loom::poll(worker, {.messages = 1, .tasks = 1})) std::this_thread::yield();
    }
    CHECK(drained && !worker.memory && !worker.arena);
    CHECK(!orbital::worker::pending(worker.lane));
    CHECK(arena->charged_bytes.load() == 0);
    loom::relinquish(worker); finished = true;
  }

  ~Fixture() { finish(); }
};

std::uint64_t expected_checksum(std::uint64_t identity) {
  std::uint64_t result{};
  for (std::uint64_t i = 0; i < 64; ++i) result += (identity * 13 + i * 7 + 5) % 256;
  return result;
}

struct Job {
  loom::Task task;
  Fixture* fixture;
  std::uint64_t identity, attempts{}, capacity_failures{}, successes{}, checksum{};
  alloc::Allocation allocation;
  alloc::Allocation* release_after_failure{};

  Job(Fixture& owner, std::uint64_t id) : fixture(&owner), identity(id) {
    task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& job = *static_cast<Job*>(task.context);
      auto& owner = *job.fixture;
      auto& signal = owner.worker.memory->capacity_changed;
      const auto observed = signal.generation;
      ++job.attempts;
      auto result = loom::allocate(owner.worker, owner.request);
      if (!result) {
        if (result.error() != Error::capacity) return Progress::fail(result.error());
        ++job.capacity_failures;
        // A real resource change between failed check and enrollment. The
        // generation check must queue a retry rather than lose this wake.
        if (job.release_after_failure) {
          loom::release(*job.release_after_failure);
          job.release_after_failure = nullptr;
        }
        return Progress::wait(signal, observed);
      }
      job.allocation = std::move(*result);
      auto bytes = loom::bytes(job.allocation);
      for (std::uint64_t i = 0; i < 64; ++i) {
        bytes[i] = std::byte{static_cast<unsigned char>((job.identity * 13 + i * 7 + 5) & 255)};
        job.checksum += std::to_integer<unsigned char>(bytes[i]);
      }
      ++job.successes;
      return Progress::done();
    };
  }

  ~Job() { CHECK(!task.worker && !task.next && !task.wait.source); }
};

// Physical input survives owner Allocation retirement, detach and Pool close.
// The final foreign Hold uses Orbital's independent return Message.
struct ForeignRelease {
  std::atomic<bool> go{};
  std::uint64_t checksum{};
  std::thread thread;

  explicit ForeignRelease(alloc::Hold use)
      : thread([this, hold = std::move(use)]() mutable {
          go.wait(false, std::memory_order_acquire);
          const auto bytes = loom::bytes(hold);
          for (std::size_t i = 0; i < 64; ++i) checksum += std::to_integer<unsigned char>(bytes[i]);
          loom::release(hold);
        }) {}

  void finish() {
    go.store(true, std::memory_order_release); go.notify_one();
    if (thread.joinable()) thread.join();
  }

  ~ForeignRelease() { finish(); }
};

void prepare_foreign_bytes(alloc::Allocation& value) {
  std::fill_n(loom::bytes(value).begin(), 64, std::byte{0x6d});
}

std::size_t waiters(const orbital::Signal& source) {
  std::size_t count{};
  for (auto* link = source.waiters; link; link = link->next) {
    CHECK(link->source == &source && ++count <= 128);
  }
  return count;
}

std::size_t queued(const loom::Worker& worker) {
  std::size_t count{};
  for (auto* task = worker.first; task; task = task->next) {
    CHECK(task->state == TaskState::queued && !task->wait.source && ++count <= 128);
  }
  return count;
}

void pressure(std::size_t count, bool foreign) {
  Fixture fixture(count);
  auto& worker = fixture.worker;
  auto& signal = worker.memory->capacity_changed;
  std::vector<std::unique_ptr<Job>> jobs;
  for (std::size_t i = 0; i < count; ++i) {
    jobs.push_back(std::make_unique<Job>(fixture, i + 1));
    CHECK(loom::start(worker, jobs.back()->task));
  }
  for (std::size_t i = 0; i < count; ++i)
    CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(waiters(signal) == count && !queued(worker));
  CHECK(worker.active_tasks == count);
  for (const auto& job : jobs) CHECK(job->attempts == 1 && job->capacity_failures == 1);

  auto victim = fixture.take_one();
  std::unique_ptr<ForeignRelease> borrower;
  if (foreign) {
    prepare_foreign_bytes(victim);
    borrower = std::make_unique<ForeignRelease>(required(loom::hold(victim, {0, fixture.page})));
  }
  const auto before_generation = signal.generation;
  loom::release(victim);
  std::uint64_t return_poll_count{};
  if (foreign) {
    CHECK(waiters(signal) == count && !queued(worker));
    borrower->finish();
    CHECK(borrower->checksum == 64 * 0x6d && orbital::worker::pending(worker.lane));
    return_poll_count = loom::poll(worker, {.messages = 1, .tasks = 0});
    CHECK(return_poll_count == 1 && !orbital::worker::pending(worker.lane));
  }
  const auto first_generations = signal.generation - before_generation;
  CHECK(first_generations >= 1 && !waiters(signal) && queued(worker) == count);
  if (!foreign) CHECK(first_generations == 1);
  // No Task step ran during release or the Message-only poll. Every enrolled
  // link was synchronously notified into the queue, despite zero Task budget.
  for (const auto& job : jobs) CHECK(job->attempts == 1);

  std::uint64_t wakes = count, retries{}, completed{}, first_retry_failures{};
  for (std::size_t remaining = count; remaining; --remaining) {
    CHECK(queued(worker) == remaining && worker.active_tasks == remaining);
    for (std::size_t i = 0; i < remaining; ++i) {
      CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
      ++retries;
    }
    Job* winner = nullptr;
    std::uint64_t successes{};
    for (const auto& job : jobs) {
      successes += job->successes;
      if (job->allocation) { CHECK(!winner); winner = job.get(); }
    }
    CHECK(winner && successes == completed + 1);
    CHECK(winner->task.state == TaskState::done && winner->checksum == expected_checksum(winner->identity));
    ++completed;
    CHECK(!queued(worker) && waiters(signal) == remaining - 1);
    if (remaining == count) first_retry_failures = remaining - 1;
    loom::release(winner->allocation);
    CHECK(!waiters(signal) && queued(worker) == remaining - 1);
    wakes += remaining - 1;
  }
  const auto expected_retries = count * (count + 1) / 2;
  CHECK(completed == count && retries == expected_retries && wakes == expected_retries);
  std::uint64_t attempts{}, failures{};
  for (const auto& job : jobs) {
    CHECK(job->task.state == TaskState::done && job->successes == 1 && !job->allocation);
    attempts += job->attempts; failures += job->capacity_failures;
  }
  CHECK(attempts == count + expected_retries && failures == attempts - count);
  fixture.finish();
  std::printf("capacity path=%s waiters=%zu initial_freed_allocations=1 first_generations=%llu "
              "first_wakes=%zu return_poll_count=%llu return_task_budget=0 first_retries=%zu "
              "first_retry_failures=%llu wake_total=%llu retry_total=%llu completed=%llu\n",
      foreign ? "foreign_hold_return" : "owner_release", count,
      static_cast<unsigned long long>(first_generations), count,
      static_cast<unsigned long long>(return_poll_count), count,
      static_cast<unsigned long long>(first_retry_failures), static_cast<unsigned long long>(wakes),
      static_cast<unsigned long long>(retries), static_cast<unsigned long long>(completed));
}

void observed_before_failed_check() {
  Fixture fixture(1);
  auto victim = fixture.take_one();
  Job job(fixture, 73);
  job.release_after_failure = &victim;
  CHECK(loom::start(fixture.worker, job.task));
  const auto generation = fixture.worker.memory->capacity_changed.generation;
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(!victim && job.attempts == 1 && job.task.state == TaskState::queued);
  CHECK(fixture.worker.memory->capacity_changed.generation == generation + 1);
  CHECK(!waiters(fixture.worker.memory->capacity_changed));
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(job.task.state == TaskState::done && job.attempts == 2 && job.successes == 1);
  CHECK(job.checksum == expected_checksum(job.identity));
  loom::release(job.allocation);
  fixture.finish();
}

struct Cell {
  alignas(Job) std::array<std::byte, sizeof(Job)> storage;
  Job* job{};

  void construct(Fixture& fixture, std::uint64_t id) {
    CHECK(!job); job = std::construct_at(reinterpret_cast<Job*>(storage.data()), fixture, id);
  }

  void destroy() { if (job) { std::destroy_at(job); job = nullptr; } }

  ~Cell() { destroy(); }
};

void detach_close_and_physical_return() {
  constexpr std::size_t count = 17;
  Fixture fixture(count);
  auto& worker = fixture.worker;
  auto& signal = worker.memory->capacity_changed;
  Cell reused;
  reused.construct(fixture, 100);
  std::vector<std::unique_ptr<Job>> jobs;
  CHECK(loom::start(worker, reused.job->task));
  for (std::size_t i = 1; i < count; ++i) {
    jobs.push_back(std::make_unique<Job>(fixture, i));
    CHECK(loom::start(worker, jobs.back()->task));
  }
  for (std::size_t i = 0; i < count; ++i)
    CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(waiters(signal) == count);
  auto victim = fixture.take_one(); prepare_foreign_bytes(victim);
  ForeignRelease borrower(required(loom::hold(victim, {0, fixture.page})));
  loom::release(victim);
  CHECK(waiters(signal) == count);

  const auto* old_address = &reused.job->task;
  loom::detach(reused.job->task); reused.destroy();
  reused.construct(fixture, 999); // A fresh Task at exactly the retired address.
  CHECK(&reused.job->task == old_address && loom::start(worker, reused.job->task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(reused.job->attempts == 1 && waiters(signal) == count);
  for (const auto index : {2u, 7u, 15u}) {
    CHECK(jobs[index]->task.state == TaskState::waiting);
    loom::detach(jobs[index]->task); jobs[index].reset();
  }
  CHECK(waiters(signal) == count - 3);
  const auto before_close = signal.generation;
  const auto closing = alloc::close(*worker.memory);
  CHECK(closing.kind() == Progress::Kind::waiting && worker.memory->state == alloc::Pool::State::closing);
  CHECK(signal.generation > before_close && !waiters(signal) && queued(worker) == count - 3);

  auto* first = worker.first;
  auto* middle = first;
  for (std::size_t i = 0; i < (count - 3) / 2; ++i) middle = middle->next;
  auto* last = worker.last;
  const auto destroy_queued = [&](loom::Task* task) {
    auto* job = static_cast<Job*>(task->context);
    CHECK(task->state == TaskState::queued && job->attempts == 1);
    loom::detach(*task);
    if (job == reused.job) reused.destroy();
    else {
      auto found = std::find_if(jobs.begin(), jobs.end(), [job](const auto& value) { return value.get() == job; });
      CHECK(found != jobs.end()); found->reset();
    }
  };
  CHECK(first != middle && first != last && middle != last);
  destroy_queued(first); destroy_queued(middle); destroy_queued(last);
  constexpr auto survivors = count - 6;
  CHECK(queued(worker) == survivors && worker.active_tasks == survivors);
  borrower.finish();
  CHECK(borrower.checksum == 64 * 0x6d && orbital::worker::pending(worker.lane));
  const auto before_return = signal.generation;
  CHECK(loom::poll(worker, {.messages = 1, .tasks = 0}) == 1);
  CHECK(signal.generation > before_return && !waiters(signal) && queued(worker) == survivors);
  for (std::size_t i = 0; i < survivors; ++i)
    CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  for (const auto& job : jobs) if (job)
    CHECK(job->task.state == TaskState::failed && job->task.failure == Error::unavailable && job->attempts == 2);
  if (reused.job)
    CHECK(reused.job->task.state == TaskState::failed && reused.job->task.failure == Error::unavailable && reused.job->attempts == 2);
  CHECK(!worker.active_tasks && !worker.first && !worker.last && !waiters(signal));
  fixture.finish();
  std::printf("capacity close_waiters=%zu detached_waiting=3 detached_queued=3 "
              "same_address_fresh_task=1 physical_return_messages=1 terminal_retries=%zu\n", count - 3, survivors);
}
}

int main() {
  Watchdog watchdog;
  for (const auto count : {1u, 8u, 64u}) {
    pressure(count, false);
    pressure(count, true);
  }
  observed_before_failed_check();
  detach_close_and_physical_return();
  std::printf("capacity checks passed (%zu checks); counters are logical work, not timings\n", checks);
}
