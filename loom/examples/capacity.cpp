#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <source_location>
#include <thread>
#include <vector>
#include <unistd.h>

// A caller-owned coordinator for 64 equal-shape requests. Only the coordinator
// waits on actual Pool capacity. Request records outlive client scheduling
// interest and keep every granted Allocation until its owner consumes/releases it.
namespace {
namespace alloc = orbital::allocation;
using orbital::Error;
using orbital::Progress;
using loom::TaskState;
constexpr std::size_t request_count = 64;
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

struct Watchdog {
  std::mutex mutex;
  std::condition_variable changed;
  bool done{};
  std::thread thread{[this] {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(30), [this] { return done; })) {
      std::fputs("coordinator caller failed its correctness deadline\n", stderr);
      std::abort();
    }
  }};

  ~Watchdog() {
    { std::lock_guard lock(mutex); done = true; }
    changed.notify_one(); thread.join();
  }
};

struct Fixture {
  std::size_t page{static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))};
  orbital::Ref<alloc::Arena> arena{required(alloc::open_arena(page * 40))};
  loom::Worker worker;
  alloc::AllocationRequest shape{.bytes = page, .reserved_bytes = page,
      .alignment = page, .initialization = alloc::Initialization::zero};
  std::vector<alloc::Allocation> occupied;
  std::uint64_t client_visits{}, direct_consumptions{};
  unsigned depth{}, maximum_depth{};
  bool finished{};

  Fixture() {
    CHECK(page > 3072 && page <= (1u << 20));
    CHECK(loom::adopt(worker));
    CHECK(loom::configure(worker, arena, request_count + 1));
    occupied.reserve(64);
    bool saturated = false;
    for (unsigned i = 0; i < 256; ++i) {
      auto storage = loom::allocate(worker, shape);
      if (!storage) { CHECK(storage.error() == Error::capacity); saturated = true; break; }
      occupied.push_back(std::move(*storage));
    }
    CHECK(saturated && !occupied.empty() && !orbital::worker::pending(worker.lane));
  }

  alloc::Allocation take_one() {
    auto result = std::move(occupied.back()); occupied.pop_back(); return result;
  }

  void finish() {
    if (finished) return;
    CHECK(!worker.active_tasks && !worker.first && !worker.last && !depth);
    occupied.clear();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      CHECK(std::chrono::steady_clock::now() < deadline);
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != Progress::Kind::failed);
      if (progress.kind() == Progress::Kind::done) break;
      if (!loom::poll(worker, {.messages = 1, .tasks = 1})) std::this_thread::yield();
    }
    CHECK(!worker.memory && !worker.arena && !orbital::worker::pending(worker.lane));
    CHECK(arena->charged_bytes.load() == 0);
    loom::relinquish(worker); finished = true;
  }

  ~Fixture() { finish(); }
};

struct Visit {
  Fixture& owner;

  explicit Visit(Fixture& value) : owner(value) {
    CHECK(++owner.depth == 1);
    owner.maximum_depth = std::max(owner.maximum_depth, owner.depth);
  }

  ~Visit() { CHECK(owner.depth == 1); --owner.depth; }
};

std::uint64_t expected(std::uint64_t identity) {
  std::uint64_t sum{};
  for (std::uint64_t i = 0; i < 64; ++i) sum += (identity * 13 + i * 7 + 5) % 256;
  return sum;
}

struct Request {
  Fixture* owner{};
  loom::Task client;
  orbital::Signal completion;
  alloc::Allocation allocation;
  std::uint64_t identity{}, visits{}, checksum{};
  bool submitted{}, consumed{};

  void consume() {
    CHECK(allocation && !consumed && completion.state == orbital::Readiness::ready);
    {
      auto bytes = loom::bytes(allocation);
      CHECK(bytes.size() == owner->page);
      for (std::uint64_t i = 0; i < 64; ++i) {
        CHECK(bytes[i] == std::byte{});
        bytes[i] = std::byte{static_cast<unsigned char>((identity * 13 + i * 7 + 5) & 255)};
        checksum += std::to_integer<unsigned char>(bytes[i]);
      }
    } // End native byte access before returning the physical credit.
    loom::release(allocation);
    consumed = true;
  }

  void configure(Fixture& fixture, std::uint64_t id) {
    owner = &fixture; identity = id;
    completion.lane = &fixture.worker.lane;
    completion.state = orbital::Readiness::pending;
    client.context = this;
    client.step = [](loom::Task& task) noexcept {
      auto& request = *static_cast<Request*>(task.context);
      Visit visit(*request.owner);
      ++request.visits; ++request.owner->client_visits;
      const auto observed = request.completion.generation;
      if (!request.allocation) return Progress::wait(request.completion, observed);
      request.consume();
      return Progress::done();
    };
  }

  ~Request() { CHECK(!client.worker && !client.next && !client.wait.source && !allocation); }
};

struct Coordinator {
  Fixture& owner;
  loom::Task task;
  std::array<Request*, request_count> queue{};
  std::size_t head{}, pending{};
  std::uint64_t visits{}, attempts{}, capacity_failures{}, grants{}, refusals{};

  explicit Coordinator(Fixture& fixture) : owner(fixture) {
    task.context = this;
    task.step = [](loom::Task& task) noexcept {
      auto& coordinator = *static_cast<Coordinator*>(task.context);
      Visit visit(coordinator.owner);
      CHECK(coordinator.pending);
      ++coordinator.visits;
      auto& capacity = coordinator.owner.worker.memory->capacity_changed;
      const auto observed = capacity.generation;
      ++coordinator.attempts;
      auto storage = loom::allocate(coordinator.owner.worker, coordinator.owner.shape);
      if (!storage) {
        CHECK(storage.error() == Error::capacity);
        ++coordinator.capacity_failures;
        return Progress::wait(capacity, observed);
      }
      auto& request = *coordinator.queue[coordinator.head];
      coordinator.queue[coordinator.head] = nullptr;
      coordinator.head = (coordinator.head + 1) % request_count;
      --coordinator.pending;
      CHECK(request.submitted && !request.allocation && !request.consumed);
      request.allocation = std::move(*storage);
      ++coordinator.grants;
      CHECK(request.client.state == TaskState::waiting && request.visits == 1);
      orbital::complete(request.completion, {});
      CHECK(request.client.state == TaskState::queued && request.visits == 1);
      // At most one grant per visit. FIFO puts this client before the yielded
      // coordinator, so this equal-shape fixture returns its one physical credit
      // before the next attempt. This is explicit caller policy, not a fit search.
      return coordinator.pending ? Progress::yield() : Progress::done();
    };
  }

  bool offer(Request& request) {
    if (pending == request_count || request.submitted) { ++refusals; return false; }
    CHECK(!request.allocation && !request.consumed && request.client.state == TaskState::idle);
    queue[(head + pending) % request_count] = &request;
    ++pending; request.submitted = true;
    return true;
  }

  ~Coordinator() { CHECK(!task.worker && !task.wait.source && !pending); }
};

struct ForeignRelease {
  std::atomic<bool> go{};
  std::uint64_t checksum{};
  std::thread thread;

  explicit ForeignRelease(alloc::Hold use)
      : thread([this, hold = std::move(use)]() mutable {
          go.wait(false, std::memory_order_acquire);
          {
            const auto bytes = loom::bytes(hold);
            for (std::size_t i = 0; i < 64; ++i)
              checksum += std::to_integer<unsigned char>(bytes[i]);
          }
          loom::release(hold);
        }) {}

  void finish() {
    go.store(true, std::memory_order_release); go.notify_one();
    if (thread.joinable()) thread.join();
  }

  ~ForeignRelease() { finish(); }
};

std::size_t waiters(const orbital::Signal& signal) {
  std::size_t result{};
  for (auto* link = signal.waiters; link; link = link->next) {
    CHECK(link->source == &signal && ++result <= request_count + 1);
  }
  return result;
}

void run(bool detach_granted_client) {
  Fixture fixture;
  std::array<Request, request_count> requests;
  Request refused;
  Coordinator coordinator(fixture);
  for (std::size_t i = 0; i < request_count; ++i) {
    requests[i].configure(fixture, i + 1);
    CHECK(coordinator.offer(requests[i]));
    CHECK(loom::start(fixture.worker, requests[i].client));
  }
  refused.configure(fixture, 999);
  const auto refused_generation = refused.completion.generation;
  CHECK(!coordinator.offer(refused));
  CHECK(!refused.submitted && !refused.allocation && refused.client.state == TaskState::idle);
  CHECK(refused.completion.generation == refused_generation && !refused.completion.waiters);
  CHECK(coordinator.pending == request_count && coordinator.refusals == 1);
  CHECK(loom::start(fixture.worker, coordinator.task));
  CHECK(!coordinator.visits && !fixture.client_visits); // Admission ran no body.
  CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = request_count + 1}) == request_count + 1);
  auto& capacity = fixture.worker.memory->capacity_changed;
  CHECK(waiters(capacity) == 1 && capacity.waiters == &coordinator.task.wait);
  CHECK(coordinator.task.state == TaskState::waiting && coordinator.attempts == 1);
  CHECK(!fixture.worker.first && fixture.worker.active_tasks == request_count + 1);
  for (const auto& request : requests)
    CHECK(request.client.state == TaskState::waiting && waiters(request.completion) == 1);

  auto victim = fixture.take_one();
  std::fill_n(loom::bytes(victim).begin(), 64, std::byte{0x6d});
  ForeignRelease borrower(required(loom::hold(victim, {0, fixture.page})));
  loom::release(victim);
  CHECK(waiters(capacity) == 1 && !fixture.worker.first);
  const auto generation = capacity.generation;
  borrower.finish();
  CHECK(borrower.checksum == 64 * 0x6d && orbital::worker::pending(fixture.worker.lane));
  const auto returned = loom::poll(fixture.worker, {.messages = 1, .tasks = 0});
  CHECK(returned == 1 && !orbital::worker::pending(fixture.worker.lane));
  CHECK(capacity.generation > generation && !capacity.waiters);
  CHECK(fixture.worker.first == &coordinator.task && fixture.worker.last == &coordinator.task);
  CHECK(coordinator.task.state == TaskState::queued && !coordinator.task.next);
  CHECK(coordinator.visits == 1 && fixture.client_visits == request_count);
  for (const auto& request : requests)
    CHECK(request.client.state == TaskState::waiting && request.visits == 1);

  for (std::size_t i = 0; i < request_count; ++i) {
    auto& request = requests[i];
    CHECK(fixture.worker.first == &coordinator.task);
    CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 1}) == 1);
    CHECK(coordinator.grants == i + 1 && fixture.worker.first == &request.client);
    CHECK(request.allocation && request.client.state == TaskState::queued && !request.completion.waiters);
    if (detach_granted_client && i == 0) {
      // End scheduling interest; the retained Request still owns its Allocation.
      // Its caller finishes the native use/release explicitly at this boundary.
      loom::detach(request.client);
      CHECK(request.client.state == TaskState::detached && !request.client.worker);
      request.consume(); ++fixture.direct_consumptions;
    } else {
      CHECK(loom::poll(fixture.worker, {.messages = 0, .tasks = 1}) == 1);
      CHECK(request.client.state == TaskState::done && request.visits == 2);
    }
    CHECK(request.consumed && !request.allocation && request.checksum == expected(request.identity));
    CHECK(!capacity.waiters);
  }
  CHECK(coordinator.task.state == TaskState::done && !coordinator.pending);
  CHECK(coordinator.visits == request_count + 1 && coordinator.attempts == request_count + 1);
  CHECK(coordinator.capacity_failures == 1 && coordinator.grants == request_count);
  CHECK(fixture.client_visits == request_count * 2 - detach_granted_client);
  CHECK(fixture.direct_consumptions == std::uint64_t(detach_granted_client));
  CHECK(fixture.maximum_depth == 1 && !fixture.depth && !fixture.worker.active_tasks);
  for (auto* entry : coordinator.queue) CHECK(!entry);
  fixture.finish();
  std::printf("coordinator caller detached=%u queue_capacity=64 accepted=64 refused=1 "
      "initial_freed_allocations=1 physical_return_messages=%llu return_task_budget=0 "
      "capacity_wakes=1 coordinator_visits=%llu allocation_attempts=%llu capacity_failures=%llu "
      "grants=%llu client_initial_waits=64 client_completion_retries=%llu client_visits=%llu "
      "direct_consumptions=%llu completed_requests=64 maximum_depth=1 charged_after_drain=0\n",
      unsigned(detach_granted_client), static_cast<unsigned long long>(returned),
      static_cast<unsigned long long>(coordinator.visits), static_cast<unsigned long long>(coordinator.attempts),
      static_cast<unsigned long long>(coordinator.capacity_failures), static_cast<unsigned long long>(coordinator.grants),
      static_cast<unsigned long long>(fixture.client_visits - request_count),
      static_cast<unsigned long long>(fixture.client_visits), static_cast<unsigned long long>(fixture.direct_consumptions));
}
} // namespace

int main() {
  Watchdog watchdog;
  run(false); run(true);
  std::printf("coordinator caller passed (%zu checks); logical counts only, no timing claim\n", checks);
}
