#include <loom/worker.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <source_location>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>
#include <unistd.h>

namespace {
using orbital::Error;
using orbital::Progress;
using loom::Task;
using loom::TaskState;
static_assert(sizeof(Progress) == 2 * sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Progress>);
std::size_t checks{};
void check(bool valid, const char* expression,
    std::source_location where = std::source_location::current()) {
  ++checks;
  if (valid) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}

#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class T> T required(orbital::Result<T> result) {
  CHECK(result);
  return std::move(*result);
}

std::size_t page_size() {
  const long size = ::sysconf(_SC_PAGESIZE);
  CHECK(size > 0);
  return static_cast<std::size_t>(size);
}

struct Fixture {
  orbital::Ref<orbital::allocation::Arena> arena;
  loom::Worker worker;

  explicit Fixture(std::uint64_t task_capacity = 4, std::uint64_t byte_limit = 1 << 20)
      : arena(required(orbital::allocation::open_arena(byte_limit))) {
    CHECK(loom::adopt(worker));
    CHECK(loom::configure(worker, arena, task_capacity));
  }

  ~Fixture() {
    bool done = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != Progress::Kind::failed);
      if (progress.kind() == Progress::Kind::done) { done = true; break; }
      if (!loom::poll(worker, {.messages = 128, .tasks = 128})) std::this_thread::yield();
    }
    CHECK(done);
    CHECK(!worker.memory && !worker.arena && !worker.active_tasks);
    CHECK(!worker.first && !worker.last && !worker.capacity_changed.waiters);
    CHECK(!orbital::worker::pending(worker.lane));
    loom::relinquish(worker);
  }
};

auto snapshot(const Task& task) {
  return std::tuple(task.worker, task.context, task.step, task.next,
      task.wait.source, task.wait.next, task.wait.waiter, task.wait.notify,
      task.state, task.failure);
}

struct WaitJob {
  Task task;
  orbital::Signal* signal{};
  unsigned calls{};
  bool ready{};
  bool pulse_before_wait{};

  explicit WaitJob(orbital::Signal& source) : signal(&source) {
    task.context = this;
    task.step = [](Task& task) noexcept {
      auto& job = *static_cast<WaitJob*>(task.context);
      ++job.calls;
      if (job.ready) return Progress::done();
      const auto observed = job.signal->generation;
      if (job.pulse_before_wait) {
        job.pulse_before_wait = false;
        orbital::pulse(*job.signal);
        job.ready = true;
      }
      return Progress::wait(*job.signal, observed);
    };
  }
};

struct CountMessage {
  orbital::worker::Message message;
  orbital::worker::Lane* lane{};
  unsigned retained{}, handled{}, released{};
  static const orbital::worker::MessageOps operations;

  explicit CountMessage(orbital::worker::Lane& owner) : lane(&owner) {
    message.context = this;
    message.ops = &operations;
  }
};

const orbital::worker::MessageOps CountMessage::operations{
    [](orbital::worker::Message& message) noexcept {
      ++static_cast<CountMessage*>(message.context)->retained;
    },
    [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
      auto& record = *static_cast<CountMessage*>(message.context);
      CHECK(record.lane == &lane && orbital::worker::on_lane(lane));
      ++record.handled;
    },
    [](orbital::worker::Message& message) noexcept {
      ++static_cast<CountMessage*>(message.context)->released;
    }};

std::uint64_t fill(orbital::MutableBytes bytes) {
  std::uint64_t expected{};
  for (std::size_t i = 0; i != bytes.size(); ++i) {
    const auto value = static_cast<unsigned char>((i * 37 + 19) & 255);
    bytes[i] = std::byte{value};
    expected += value;
  }
  return expected;
}

// A real physical borrower reads after owner cancellation/release. Its final
// Hold return uses Orbital's reserved home record, not a Task-embedded message.
struct ForeignUse {
  std::atomic<bool> go{};
  std::atomic<std::uint64_t> checksum{};
  std::thread thread;

  explicit ForeignUse(orbital::allocation::Hold hold)
      : thread([this, storage = std::move(hold)]() mutable {
          go.wait(false, std::memory_order_acquire);
          std::uint64_t sum{};
          for (const auto value : orbital::allocation::bytes(storage))
            sum += std::to_integer<unsigned char>(value);
          checksum.store(sum, std::memory_order_relaxed);
          orbital::allocation::release(storage);
        }) {}

  void finish() {
    go.store(true, std::memory_order_release);
    go.notify_one();
    if (thread.joinable()) thread.join();
  }

  ~ForeignUse() { finish(); }
};

void admission_and_physical_progress() {
  const auto page = page_size();
  Fixture fixture(1);
  auto& worker = fixture.worker;
  orbital::Signal source;
  source.lane = &worker.lane;
  WaitJob accepted(source), refused(source);
  CHECK(loom::start(worker, accepted.task));
  CHECK(!accepted.calls && worker.active_tasks == 1);
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(accepted.task.state == TaskState::waiting);
  const auto before = snapshot(refused.task);
  const auto generation = worker.capacity_changed.generation;
  CHECK(!loom::start(worker, refused.task));
  CHECK(snapshot(refused.task) == before);
  CHECK(worker.active_tasks == 1 && worker.capacity_changed.generation == generation);

  auto allocation = required(loom::allocate(worker,
      {.bytes = page, .reserved_bytes = page, .alignment = page}));
  const auto expected = fill(loom::bytes(allocation));
  // Saturate the actual allocator, including its real control charge. Do not
  // assume one payload page is the Arena's entire physical accounting unit.
  std::vector<orbital::allocation::Allocation> occupied;
  bool saturated = false;
  for (unsigned i = 0; i != 1024; ++i) {
    auto extra = loom::allocate(worker,
        {.bytes = page, .reserved_bytes = page, .alignment = page});
    if (!extra) {
      CHECK(extra.error() == Error::capacity);
      saturated = true;
      break;
    }
    occupied.push_back(std::move(*extra));
  }
  CHECK(saturated);
  auto hold = required(loom::hold(allocation, {0, page}));
  ForeignUse peer(std::move(hold));
  loom::release(allocation);
  auto blocked = loom::allocate(worker, {.bytes = page, .reserved_bytes = page, .alignment = page});
  CHECK(!blocked && blocked.error() == Error::capacity);
  peer.finish();
  CHECK(peer.checksum.load() == expected);
  CHECK(orbital::worker::pending(worker.lane));
  CHECK(loom::poll(worker, {.messages = 128, .tasks = 0}) > 0);
  CHECK(worker.active_tasks == 1 && accepted.task.state == TaskState::waiting);
  auto reused = required(loom::allocate(worker,
      {.bytes = page, .reserved_bytes = page, .alignment = page}));
  CHECK(loom::bytes(reused).size() == page);
  loom::release(reused);
  CHECK(!loom::start(worker, refused.task) && snapshot(refused.task) == before);
  loom::detach(accepted.task);
  CHECK(!worker.active_tasks && !source.waiters);
  CHECK(loom::start(worker, refused.task));
  refused.ready = true;
  CHECK(loom::poll(worker, {.messages = 128, .tasks = 1}) >= 1);
  CHECK(refused.task.state == TaskState::done && !refused.task.worker);
  occupied.clear();
}

void generation_and_terminal_retry() {
  Fixture fixture(1);
  auto& worker = fixture.worker;
  orbital::Signal source;
  source.lane = &worker.lane;
  WaitJob raced(source);
  raced.pulse_before_wait = true;
  CHECK(loom::start(worker, raced.task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(raced.calls == 1 && raced.task.state == TaskState::queued && !source.waiters);
  CHECK(worker.active_tasks == 1);
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(raced.task.state == TaskState::done && raced.calls == 2);

  for (const auto outcome : {orbital::Readiness::ready, orbital::Readiness::failed,
      orbital::Readiness::cancelled}) {
    orbital::Signal terminal;
    terminal.lane = &worker.lane;
    if (outcome == orbital::Readiness::failed) orbital::complete(terminal, std::unexpected(Error::io));
    else if (outcome == orbital::Readiness::ready) orbital::complete(terminal, {});
    else { terminal.state = outcome; orbital::pulse(terminal); }
    WaitJob retry(terminal);
    CHECK(loom::start(worker, retry.task));
    CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
    CHECK(retry.task.state == TaskState::queued && retry.calls == 1 && !terminal.waiters);
    retry.ready = true;
    CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
    // A terminal source wakes an operation to interpret its result; it does
    // not impose the Signal outcome on the Task's own outcome.
    CHECK(retry.task.state == TaskState::done && retry.calls == 2);
  }

  orbital::worker::Lane other_lane;
  orbital::Signal foreign;
  foreign.lane = &other_lane;
  WaitJob invalid(foreign);
  CHECK(loom::start(worker, invalid.task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(invalid.task.state == TaskState::failed && invalid.task.failure == Error::invalid_argument);
  CHECK(!invalid.task.worker && !worker.active_tasks && !foreign.waiters);
}

void full_generation_waits() {
  Fixture fixture(1);
  auto& worker = fixture.worker;
  orbital::Signal source;
  source.lane = &worker.lane;
  // A retry observation must preserve every generation bit, including a stale
  // value that differs only above bit 31. No generation wraps while live.
  source.generation = (std::uint64_t{1} << 63) | (std::uint64_t{1} << 32) | 7;
  struct Job { Task task; orbital::Signal* source; std::uint64_t observed; } job;
  job.source = &source;
  job.observed = 7;
  job.task.context = &job;
  job.task.step = [](Task& task) noexcept {
    auto& job = *static_cast<Job*>(task.context);
    return Progress::wait(*job.source, job.observed);
  };
  CHECK(loom::start(worker, job.task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(job.task.state == TaskState::queued && !source.waiters);
  job.observed = source.generation;
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(job.task.state == TaskState::waiting && source.waiters == &job.task.wait);
  orbital::pulse(source);
  CHECK(job.task.state == TaskState::queued && !source.waiters);
  source.generation = std::numeric_limits<std::uint64_t>::max();
  job.observed = source.generation;
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(job.task.state == TaskState::waiting && source.waiters == &job.task.wait);
  loom::detach(job.task);
  CHECK(!source.waiters && !worker.active_tasks && !job.task.worker);
}

void notification_never_reenters() {
  Fixture fixture(2);
  auto& worker = fixture.worker;
  orbital::Signal source;
  source.lane = &worker.lane;
  WaitJob waiter(source);
  CHECK(loom::start(worker, waiter.task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  struct Completing {
    Task task;
    WaitJob* waiter{};
    unsigned calls{}, depth{}, maximum_depth{};
  } completing;
  completing.waiter = &waiter;
  completing.task.context = &completing;
  completing.task.step = [](Task& task) noexcept {
    auto& job = *static_cast<Completing*>(task.context);
    ++job.depth;
    if (job.depth > job.maximum_depth) job.maximum_depth = job.depth;
    ++job.calls;
    if (job.calls == 1) {
      const auto observed = job.waiter->signal->generation;
      orbital::complete(*job.waiter->signal, {});
      CHECK(job.waiter->calls == 1);
      CHECK(job.waiter->task.state == TaskState::queued);
      CHECK(!job.waiter->task.wait.source && !job.waiter->task.wait.next);
      job.waiter->ready = true;
      --job.depth;
      return Progress::wait(*job.waiter->signal, observed);
    }
    --job.depth;
    return Progress::done();
  };
  CHECK(loom::start(worker, completing.task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(completing.calls == 1 && completing.maximum_depth == 1 && waiter.calls == 1);
  CHECK(worker.first == &waiter.task && worker.last == &completing.task);
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 2}) == 2);
  CHECK(completing.task.state == TaskState::done && waiter.task.state == TaskState::done);
  CHECK(completing.maximum_depth == 1 && waiter.calls == 2);
}

void fifo_yields_and_failure() {
  Fixture fixture(3);
  struct Trace { std::array<unsigned, 9> order{}; unsigned used{}; } trace;
  struct Job { Task task; Trace* trace{}; unsigned id{}, calls{}; };
  std::array<Job, 3> jobs;
  for (unsigned i = 0; i != jobs.size(); ++i) {
    auto& job = jobs[i];
    job.trace = &trace;
    job.id = i + 1;
    job.task.context = &job;
    job.task.step = [](Task& task) noexcept {
      auto& job = *static_cast<Job*>(task.context);
      CHECK(job.trace->used < job.trace->order.size());
      job.trace->order[job.trace->used++] = job.id;
      return ++job.calls == 3 ? Progress::done() : Progress::yield();
    };
    CHECK(loom::start(fixture.worker, job.task));
  }
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 5}) == 5);
  CHECK(trace.used == 5 && fixture.worker.active_tasks == 3);
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 4}) == 4);
  CHECK((trace.order == std::array<unsigned, 9>{1, 2, 3, 1, 2, 3, 1, 2, 3}));
  CHECK(!fixture.worker.active_tasks);
  for (auto& job : jobs) {
    CHECK(job.task.state == TaskState::done && job.calls == 3);
    loom::detach(job.task);
    CHECK(!fixture.worker.active_tasks);
  }
  for (auto error : {Error::invalid_argument, Error::absent, Error::conflict,
      Error::stale, Error::unavailable, Error::capacity, Error::io, Error::rejected}) {
    Task failure;
    failure.context = &error;
    failure.step = [](Task& task) noexcept { return Progress::fail(*static_cast<const Error*>(task.context)); };
    CHECK(loom::start(fixture.worker, failure));
    CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 1}) == 1);
    CHECK(failure.state == TaskState::failed && failure.failure == error);
    const auto generation = fixture.worker.capacity_changed.generation;
    loom::detach(failure);
    loom::detach(failure);
    CHECK(!fixture.worker.active_tasks && fixture.worker.capacity_changed.generation == generation);
    CHECK(!loom::start(fixture.worker, failure));
  }
}

void detach_memberships_and_destroy() {
  Fixture fixture(5);
  orbital::Signal source;
  source.lane = &fixture.worker.lane;
  std::array<WaitJob, 5> queued{WaitJob(source), WaitJob(source), WaitJob(source),
      WaitJob(source), WaitJob(source)};
  for (auto& job : queued) CHECK(loom::start(fixture.worker, job.task));
  for (unsigned index : {0u, 2u, 4u}) {
    loom::detach(queued[index].task);
    CHECK(queued[index].task.state == TaskState::detached && !queued[index].task.worker);
    loom::detach(queued[index].task);
  }
  CHECK(fixture.worker.active_tasks == 2);
  CHECK(fixture.worker.first == &queued[1].task && fixture.worker.last == &queued[3].task);
  CHECK(queued[1].task.next == &queued[3].task && !queued[3].task.next);
  queued[1].ready = queued[3].ready = true;
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 8}) == 2);
  CHECK(!queued[0].calls && !queued[2].calls && !queued[4].calls);

  WaitJob first(source), last(source);
  auto middle = std::make_unique<WaitJob>(source);
  CHECK(loom::start(fixture.worker, first.task));
  CHECK(loom::start(fixture.worker, middle->task));
  CHECK(loom::start(fixture.worker, last.task));
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 3}) == 3);
  CHECK(source.waiters == &last.task.wait && last.task.wait.next == &middle->task.wait);
  loom::detach(middle->task);
  CHECK(!middle->task.wait.source && !middle->task.wait.next && !middle->task.wait.notify);
  middle.reset();
  first.ready = last.ready = true;
  orbital::pulse(source);
  orbital::pulse(source);
  CHECK(!source.waiters && fixture.worker.active_tasks == 2);
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 8}) == 2);
  CHECK(first.calls == 2 && last.calls == 2 && !fixture.worker.active_tasks);

  auto abandoned = std::make_unique<WaitJob>(source);
  CHECK(loom::start(fixture.worker, abandoned->task));
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 1}) == 1);
  loom::detach(abandoned->task);
  abandoned.reset();
  orbital::pulse(source);
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 8}) == 0);
  CHECK(!fixture.worker.first && !fixture.worker.last && !fixture.worker.active_tasks);
}

void direct_only_and_pending_message_shutdown() {
  Fixture fixture(0);
  auto& worker = fixture.worker;
  Task refused;
  refused.step = [](Task&) noexcept { return Progress::done(); };
  const auto before = snapshot(refused);
  CHECK(!loom::start(worker, refused) && snapshot(refused) == before);
  auto allocation = required(loom::allocate(worker, {.bytes = 64}));
  CHECK(loom::bytes(allocation).size() == 64);
  fill(loom::bytes(allocation));
  loom::release(allocation);
  loom::poll(worker, {.messages = 128, .tasks = 0});
  CHECK(!orbital::worker::pending(worker.lane));
  CountMessage completion(worker.lane);
  orbital::worker::post(worker.lane, completion.message);
  CHECK(completion.retained == 1 && !completion.handled && !completion.released);
  CHECK(loom::shutdown(worker).kind() != Progress::Kind::done);
  CHECK(loom::poll(worker, {.messages = 1, .tasks = 0}) == 1);
  CHECK(completion.handled == 1 && completion.released == 1);
}

void message_service_between_steps() {
  Fixture fixture(1);
  CountMessage completion(fixture.worker.lane);
  struct Job { Task task; CountMessage* completion{}; unsigned calls{}; } job;
  job.completion = &completion;
  job.task.context = &job;
  job.task.step = [](Task& task) noexcept {
    auto& job = *static_cast<Job*>(task.context);
    if (++job.calls == 1) {
      orbital::worker::post(task.worker->lane, job.completion->message);
      CHECK(!job.completion->handled);
      return Progress::yield();
    }
    CHECK(job.completion->handled == 1 && job.completion->released == 1);
    return Progress::done();
  };
  CHECK(loom::start(fixture.worker, job.task));
  CHECK(loom::poll(fixture.worker, {.messages = 1, .tasks = 2}) == 3);
  CHECK(job.calls == 2 && job.task.state == TaskState::done);
}

void stable_worker_handoff() {
  Fixture fixture(2);
  auto& worker = fixture.worker;
  auto* const lane_address = &worker.lane;
  auto* const pool_address = worker.memory;
  orbital::Signal source;
  source.lane = lane_address;
  WaitJob waiting(source), queued(source);
  queued.ready = true;
  CHECK(loom::start(worker, waiting.task));
  CHECK(loom::start(worker, queued.task));
  CHECK(loom::poll(worker, {.messages = 0, .tasks = 1}) == 1);
  CHECK(waiting.task.state == TaskState::waiting && queued.task.state == TaskState::queued);
  auto allocation = required(loom::allocate(worker, {.bytes = 64, .reserved_bytes = 64}));
  const auto expected = fill(loom::bytes(allocation));
  auto hold = required(loom::hold(allocation, {0, 64}));
  CountMessage completion(worker.lane);
  orbital::worker::post(worker.lane, completion.message);
  // Native pointer borrows ended above. The caller-owned frames stay stable
  // through join; retained backing and queued records keep their original home.
  loom::relinquish(worker);
  CHECK(!orbital::worker::on_lane(worker.lane));
  std::thread successor([&, allocation = std::move(allocation), hold = std::move(hold)]() mutable {
    CHECK(loom::adopt(worker));
    CHECK(&worker.lane == lane_address && worker.memory == pool_address);
    CHECK(waiting.task.worker == &worker && queued.task.worker == &worker);
    CHECK(source.lane == lane_address && completion.lane == lane_address);
    CHECK(loom::poll(worker, {.messages = 1, .tasks = 0}) == 1);
    CHECK(completion.handled == 1 && completion.released == 1);
    std::uint64_t sum{};
    for (const auto value : loom::bytes(hold)) sum += std::to_integer<unsigned char>(value);
    CHECK(sum == expected);
    waiting.ready = true;
    orbital::pulse(source);
    CHECK(worker.first == &queued.task && worker.last == &waiting.task);
    CHECK(loom::poll(worker, {.messages = 128, .tasks = 2}) >= 2);
    CHECK(waiting.task.state == TaskState::done && queued.task.state == TaskState::done);
    loom::release(allocation);
    loom::release(hold);
    loom::relinquish(worker);
  });
  successor.join();
  CHECK(loom::adopt(worker));
  CHECK(&worker.lane == lane_address && worker.memory == pool_address && !worker.active_tasks);
  CHECK(waiting.calls == 2 && queued.calls == 1);
}

void shutdown_active_and_foreign_drain() {
  const auto page = page_size();
  Fixture fixture(1);
  auto& worker = fixture.worker;
  struct AllocatingJob {
    Task task;
    orbital::allocation::Allocation storage;
    std::size_t page{};
    std::uint64_t expected{};
    unsigned calls{};
  } job;
  job.page = page;
  job.task.context = &job;
  job.task.step = [](Task& task) noexcept {
    auto& job = *static_cast<AllocatingJob*>(task.context);
    if (++job.calls == 1) {
      auto allocation = loom::allocate(*task.worker,
          {.bytes = job.page, .reserved_bytes = job.page, .alignment = job.page});
      CHECK(allocation);
      job.storage = std::move(*allocation);
      job.expected = fill(loom::bytes(job.storage));
      return Progress::yield();
    }
    return Progress::done();
  };
  CHECK(loom::start(worker, job.task));
  auto progress = loom::shutdown(worker);
  CHECK(progress.kind() == Progress::Kind::waiting && progress.signal() == &worker.capacity_changed);
  CHECK(worker.stopping && worker.active_tasks == 1);
  Task refused;
  refused.step = [](Task&) noexcept { return Progress::done(); };
  const auto before = snapshot(refused);
  CHECK(!loom::start(worker, refused) && snapshot(refused) == before);
  // Accepted work may still acquire output to finish while new starts stop.
  CHECK(loom::poll(worker, {.messages = 128, .tasks = 1}) >= 1);
  CHECK(job.calls == 1 && job.task.state == TaskState::queued);
  ForeignUse peer(required(loom::hold(job.storage, {0, page})));
  CHECK(loom::poll(worker, {.messages = 128, .tasks = 1}) >= 1);
  CHECK(job.task.state == TaskState::done && !worker.active_tasks);
  loom::release(job.storage);
  progress = loom::shutdown(worker);
  CHECK(progress.kind() != Progress::Kind::done && progress.kind() != Progress::Kind::failed);
  CHECK(worker.memory && worker.arena);
  auto denied = loom::allocate(worker, {.bytes = 1});
  CHECK(!denied);
  loom::poll(worker, {.messages = 128, .tasks = 128});
  CHECK(loom::shutdown(worker).kind() != Progress::Kind::done);
  peer.finish();
  CHECK(peer.checksum.load() == job.expected);
  CHECK(orbital::worker::pending(worker.lane));
  CHECK(loom::shutdown(worker).kind() != Progress::Kind::done);
  CHECK(loom::poll(worker, {.messages = 128, .tasks = 0}) > 0);
  bool done = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    progress = loom::shutdown(worker);
    CHECK(progress.kind() != Progress::Kind::failed);
    if (progress.kind() == Progress::Kind::done) { done = true; break; }
    if (!loom::poll(worker, {.messages = 128, .tasks = 0})) std::this_thread::yield();
  }
  CHECK(done && !worker.memory && !worker.arena);
  CHECK(loom::shutdown(worker).kind() == Progress::Kind::done);
  CHECK(!loom::start(worker, refused) && snapshot(refused) == before);
}

void small_padded_foreign_borrow() {
  Fixture fixture(0);
  auto& worker = fixture.worker;
  auto allocation = required(loom::allocate(worker, {.bytes = 1, .reserved_bytes = 32,
      .alignment = 16, .readable_padding = 15,
      .initialization = orbital::allocation::Initialization::zero}));
  CHECK(loom::bytes(allocation).size() == 1);
  CHECK(reinterpret_cast<std::uintptr_t>(loom::bytes(allocation).data()) % 16 == 0);
  loom::bytes(allocation)[0] = std::byte{90};
  auto hold = required(loom::hold(allocation, {0, 16}));
  const auto qualified = loom::bytes(hold);
  CHECK(qualified.size() == 16 && qualified[0] == std::byte{90});
  for (std::size_t i = 1; i != qualified.size(); ++i) CHECK(qualified[i] == std::byte{0});
  ForeignUse peer(std::move(hold));
  loom::release(allocation);
  CHECK(loom::shutdown(worker).kind() != Progress::Kind::done);
  peer.finish();
  CHECK(peer.checksum.load() == 90);
  CHECK(loom::poll(worker, {.messages = 128, .tasks = 0}) > 0);
}

void replacement_workers_share_arena() {
  auto arena = required(orbital::allocation::open_arena(1 << 20));
  for (unsigned replacement = 0; replacement != 32; ++replacement) {
    loom::Worker worker;
    CHECK(loom::adopt(worker));
    CHECK(loom::configure(worker, arena, 0));
    auto* const pool = worker.memory;
    auto removal = orbital::allocation::remove_pool(*pool);
    CHECK(!removal && removal.error() == Error::conflict);
    auto allocation = required(loom::allocate(worker, {.bytes = 64,
        .reserved_bytes = 80, .alignment = 16, .readable_padding = 16,
        .initialization = orbital::allocation::Initialization::zero}));
    const auto expected = fill(loom::bytes(allocation));
    ForeignUse reader(required(loom::hold(allocation, {0, 80})));
    loom::release(allocation);
    CountMessage completion(worker.lane);
    orbital::worker::post(worker.lane, completion.message);
    CHECK(loom::shutdown(worker).kind() != Progress::Kind::done);
    CHECK(worker.memory == pool && worker.arena && !completion.handled);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!completion.handled) {
      CHECK(std::chrono::steady_clock::now() < deadline);
      loom::poll(worker, {.messages = 1, .tasks = 0});
    }
    CHECK(completion.retained == 1 && completion.released == 1);
    CHECK(loom::shutdown(worker).kind() == Progress::Kind::waiting);
    removal = orbital::allocation::remove_pool(*pool);
    CHECK(!removal && removal.error() == Error::conflict);
    CHECK(worker.memory == pool && worker.arena);
    reader.finish();
    CHECK(reader.checksum.load() == expected);
    CHECK(orbital::worker::pending(worker.lane));
    // Final foreign publication still owns the Pool's physical records.
    removal = orbital::allocation::remove_pool(*pool);
    CHECK(!removal && removal.error() == Error::conflict);
    CHECK(loom::shutdown(worker).kind() != Progress::Kind::done);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != Progress::Kind::failed);
      if (progress.kind() == Progress::Kind::done) break;
      CHECK(std::chrono::steady_clock::now() < deadline);
      if (!loom::poll(worker, {.messages = 1, .tasks = 0})) std::this_thread::yield();
    }
    CHECK(!worker.memory && !worker.arena && !orbital::worker::pending(worker.lane));
    CHECK(loom::shutdown(worker).kind() == Progress::Kind::done);
    CHECK(arena->charged_bytes.load() == 0 && arena->references.load() == 1);
    loom::relinquish(worker);
    // The next Worker uses the same retained Arena after this stable Lane dies.
  }
}
} // namespace

int main() {
  admission_and_physical_progress();
  generation_and_terminal_retry();
  full_generation_waits();
  notification_never_reenters();
  fifo_yields_and_failure();
  detach_memberships_and_destroy();
  direct_only_and_pending_message_shutdown();
  message_service_between_steps();
  stable_worker_handoff();
  shutdown_active_and_foreign_drain();
  small_padded_foreign_borrow();
  replacement_workers_share_arena();
  std::printf("loom_worker_check: %zu checks passed\n", checks);
}
