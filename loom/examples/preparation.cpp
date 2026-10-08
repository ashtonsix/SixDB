#include <loom/preparation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <source_location>
#include <span>
#include <string_view>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>

// A bounded caller cohort using one placed service Lane. Native work takes
// ready bytes on the owner; the route survives cancellation and physical drain.
namespace {
namespace alloc = orbital::allocation;
using orbital::Error;
using orbital::Progress;
using Clock = std::chrono::steady_clock;
constexpr std::size_t inventory_limit = 2, padding = 64;

void check(bool valid, const char* expression,
    std::source_location where = std::source_location::current()) noexcept {
  if (valid) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
  std::abort();
}

#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class T> T required(orbital::Result<T> result) {
  CHECK(result);
  return std::move(*result);
}

std::uint64_t elapsed_ns(Clock::time_point first, Clock::time_point last) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(last - first).count());
}

auto deadline() { return Clock::now() + std::chrono::seconds(10); }

// One explicitly placed thread and Lane; it owns no allocation Pool. The Lane
// notices every post, including later phases posted by owner completion.
struct Helper {
  orbital::worker::Lane lane;
  orbital::worker::Thread thread;
  std::atomic<unsigned> startup{};
  std::atomic<bool> stopping{};
  std::atomic<std::uint64_t> epoch{};
  int requested_cpu{-1}, observed_cpu{-1}, owner_cpu{-1};
  bool alternative_cpu{};
  std::uint64_t startup_ns{}, join_ns{}, thread_cpu_ns{};

  void start() {
    lane.notify_posted = notify;
    lane.notify_context = this;
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    CHECK(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0);
    owner_cpu = ::sched_getcpu();
    CHECK(owner_cpu >= 0 && CPU_ISSET(owner_cpu, &allowed));
    requested_cpu = owner_cpu;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (CPU_ISSET(cpu, &allowed) && cpu != owner_cpu) {
        requested_cpu = cpu;
        alternative_cpu = true;
        break;
      }
    }
    const auto first = Clock::now();
    CHECK(orbital::worker::spawn(thread, entry, this, requested_cpu));
    unsigned status;
    while (!(status = startup.load(std::memory_order_acquire)))
      startup.wait(0, std::memory_order_acquire);
    CHECK(status == 1 && observed_cpu == requested_cpu);
    startup_ns = elapsed_ns(first, Clock::now());
  }

  static void entry(void* context) noexcept {
    auto& self = *static_cast<Helper*>(context);
    if (!orbital::worker::adopt(self.lane, self.requested_cpu)) {
      self.startup.store(2, std::memory_order_release);
      self.startup.notify_one();
      return;
    }
    self.observed_cpu = ::sched_getcpu();
    self.startup.store(1, std::memory_order_release);
    self.startup.notify_one();
    for (;;) {
      // Snapshot before drain closes the poll-to-wait race. An incomplete
      // publication can return zero; its sole publisher will finish post and
      // advance this epoch. Empty queue state is never publisher shutdown.
      const auto observed = self.epoch.load(std::memory_order_acquire);
      while (orbital::worker::poll(self.lane, 1)) {}
      if (self.stopping.load(std::memory_order_acquire)) {
        CHECK(!orbital::worker::pending(self.lane));
        break;
      }
      self.epoch.wait(observed, std::memory_order_acquire);
    }
    orbital::worker::relinquish(self.lane);
    timespec used{};
    CHECK(::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &used) == 0);
    // Thread-clock total includes spawn's affinity setup before this entry.
    // Capture immediately before return; no per-job/native-read clock exists.
    self.thread_cpu_ns = std::uint64_t(used.tv_sec) * 1000000000ull + used.tv_nsec;
  }

  static void notify(void* context) noexcept {
    auto& self = *static_cast<Helper*>(context);
    self.epoch.fetch_add(1, std::memory_order_release);
    self.epoch.notify_one();
  }

  void stop_after_physical_settle() {
    CHECK(thread.native.joinable());
    const auto first = Clock::now();
    stopping.store(true, std::memory_order_release);
    epoch.fetch_add(1, std::memory_order_release);
    epoch.notify_one();
    CHECK(orbital::worker::join(thread));
    join_ns = elapsed_ns(first, Clock::now());
    CHECK(!orbital::worker::pending(lane));
  }

  ~Helper() { CHECK(!thread.native.joinable()); }
};

struct Context {
  orbital::worker::Lane* owner{};
  std::size_t body_bytes{};
  std::uint64_t identity{}, calls{}, checksum{};
  int observed_cpu{-1};
  bool fail{}, blocked{}, gate_timed_out{};
  std::atomic<bool> entered{}, proceed{};

  static orbital::Result<void> construct(loom::PreparedBytes prepared, void* pointer) noexcept {
    auto& self = *static_cast<Context*>(pointer);
    CHECK(!orbital::worker::on_lane(*self.owner));
    CHECK(prepared.body.size() == self.body_bytes && prepared.readable_padding == padding);
    const std::span<const std::byte> suffix{
        prepared.body.data() + prepared.body.size(), prepared.readable_padding};
    for (const auto byte : suffix) CHECK(byte == std::byte{});
    ++self.calls;
    self.observed_cpu = ::sched_getcpu();
    self.entered.store(true, std::memory_order_release);
    if (self.blocked) {
      const auto limit = deadline();
      while (!self.proceed.load(std::memory_order_acquire)) {
        if (Clock::now() >= limit) {
          self.gate_timed_out = true;
          return std::unexpected(Error::unavailable);
        }
        std::this_thread::yield();
      }
    }
    if (self.fail) {
      // Leave real partial construction behind. It must never be published as
      // ready, and physical failure cleanup must still retire these bytes.
      std::fill_n(prepared.body.data(), padding, std::byte{0xa5});
      self.checksum = padding * 0xa5;
      return std::unexpected(Error::io);
    }
    for (std::size_t i = 0; i < prepared.body.size(); ++i) {
      const auto value = static_cast<unsigned char>((self.identity * 17 + i * 5 + 11) & 255);
      prepared.body[i] = std::byte{value};
      self.checksum += value;
    }
    return {};
  }
};

struct Slot {
  loom::Preparation preparation;
  Context context;
  bool accepted{};

  ~Slot() {
    CHECK(preparation.state != loom::Preparation::State::pending);
    CHECK(!preparation.backend && !preparation.preparer && !preparation.value);
    CHECK(!preparation.changed.waiters);
  }
};

enum class Shape { window, finite };

// A finite cohort uses fresh records; it does not reset terminal Signals or
// recycle contexts while another Lane could still reach them.
struct Caller {
  loom::Worker worker;
  orbital::Ref<alloc::Arena> arena;
  Helper helper;
  std::array<Slot, inventory_limit> slots;
  std::size_t page{static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))};
  alloc::AllocationRequest shape;
  std::size_t accepted{}, maximum_inventory{};
  std::uint64_t stockouts{}, ready_takes{}, full_refusals{}, stopped_refusals{}, preparation_refusals{};
  bool accepting{true}, finished{};

  explicit Caller(Shape kind) {
    CHECK(page > 3072 && page <= (1u << 20));
    CHECK(loom::adopt(worker));
    arena = required(alloc::open_arena(page * 64));
    CHECK(loom::configure(worker, arena, 0));
    shape = kind == Shape::window
        ? alloc::AllocationRequest{page * 2, page * 3, page, padding,
            alloc::Initialization::uninitialized}
        : alloc::AllocationRequest{1024, 1088, 64, padding,
            alloc::Initialization::uninitialized};
    loom::Task refused;
    refused.step = [](loom::Task&) noexcept { CHECK(false); return Progress::done(); };
    CHECK(!loom::start(worker, refused));
    CHECK(refused.state == loom::TaskState::idle && !refused.worker && !worker.active_tasks);
    helper.start();
  }

  void owner() const { CHECK(orbital::worker::on_lane(worker.lane)); }

  std::size_t inventory() const {
    std::size_t result{};
    for (const auto& slot : slots)
      if (slot.accepted && (slot.preparation.backend ||
          slot.preparation.state == loom::Preparation::State::ready)) ++result;
    return result;
  }

  orbital::Result<std::size_t> offer(std::uint64_t identity, bool fail = false, bool blocked = false) {
    owner();
    if (!accepting) { ++stopped_refusals; return std::unexpected(Error::rejected); }
    if (accepted == slots.size()) { ++full_refusals; return std::unexpected(Error::capacity); }
    auto& slot = slots[accepted];
    auto& context = slot.context;
    context.owner = &worker.lane;
    context.body_bytes = shape.bytes;
    context.identity = identity;
    context.fail = fail;
    context.blocked = blocked;
    slot.preparation.construct = Context::construct;
    slot.preparation.context = &context;
    auto result = loom::prepare(worker, slot.preparation, shape, helper.lane);
    if (!result) {
      ++preparation_refusals;
      return std::unexpected(result.error());
    }
    slot.accepted = true;
    const auto index = accepted++;
    maximum_inventory = std::max(maximum_inventory, inventory());
    CHECK(maximum_inventory <= inventory_limit);
    return index;
  }

  void stop_admission() { owner(); accepting = false; }

  void settle() {
    owner();
    const auto limit = deadline();
    for (;;) {
      bool pending = false;
      for (const auto& slot : slots) if (slot.accepted) {
        // This caller injects constructor failure, with successful physical
        // cleanup. A VM repair failure still owns physical work and requires
        // an explicit cleanup decision; it is never a safely terminal record.
        CHECK(slot.preparation.state != loom::Preparation::State::repair_wait);
        pending |= slot.preparation.backend != nullptr;
      }
      if (!pending) break;
      CHECK(Clock::now() < limit);
      loom::poll(worker, {.messages = 1, .tasks = 0});
      std::this_thread::yield();
    }
    for (const auto& slot : slots) if (slot.accepted) {
      CHECK(!slot.preparation.backend && !slot.preparation.preparer);
      CHECK(slot.context.calls == 1 && !slot.context.gate_timed_out);
      CHECK(slot.context.observed_cpu == helper.requested_cpu);
    }
    CHECK(!worker.active_tasks && !worker.first && !worker.last);
  }

  orbital::Result<alloc::Allocation> take_ready() {
    owner();
    for (auto& slot : slots) if (slot.preparation.state == loom::Preparation::State::ready) {
      auto result = loom::take(slot.preparation);
      CHECK(result && slot.preparation.state == loom::Preparation::State::taken);
      ++ready_takes;
      return result;
    }
    ++stockouts;
    return std::unexpected(Error::unavailable);
  }

  void verify_and_release(alloc::Allocation& value, const Context& context) {
    owner();
    // Owner capture succeeds after helper completion: the original Pool still
    // owns the Allocation. All native byte accesses end before either release.
    auto use = required(loom::hold(value, {0, value.size + value.readable_padding}));
    {
      const auto bytes = loom::bytes(use);
      CHECK(value.size == context.body_bytes && value.readable_padding == padding);
      std::uint64_t expected{};
      for (std::size_t i = 0; i < value.size; ++i) {
        const auto wanted = (context.identity * 17 + i * 5 + 11) % 256;
        CHECK(std::to_integer<unsigned char>(bytes[i]) == wanted);
        expected += wanted;
      }
      CHECK(expected == context.checksum);
      for (std::size_t i = value.size; i < bytes.size(); ++i) CHECK(bytes[i] == std::byte{});
    }
    loom::release(use);
    loom::release(value);
  }

  void finish(const char* name) {
    stop_admission();
    for (const auto& slot : slots) if (slot.accepted) {
      CHECK(slot.preparation.state != loom::Preparation::State::pending);
      CHECK(!slot.preparation.backend && !slot.preparation.preparer && !slot.preparation.value);
    }
    // No accepted physical outcome or constructor context remains reachable
    // from the helper route. Admission ended before publishing the stop flag.
    helper.stop_after_physical_settle();
    const auto limit = deadline();
    for (;;) {
      CHECK(Clock::now() < limit);
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != Progress::Kind::failed);
      if (progress.kind() == Progress::Kind::done) break;
      loom::poll(worker, {.messages = 1, .tasks = 0});
      std::this_thread::yield();
    }
    CHECK(!worker.memory && !worker.arena && !orbital::worker::pending(worker.lane));
    CHECK(arena->charged_bytes.load() == 0 && !worker.active_tasks);
    loom::relinquish(worker);
    finished = true;
    std::printf("{\"case\":\"%s\",\"passed\":true,\"task_capacity\":0,\"inventory_limit\":%zu,"
        "\"maximum_inventory\":%zu,\"accepted\":%zu,\"stockouts\":%llu,\"ready_takes\":%llu,"
        "\"full_refusals\":%llu,\"stopped_refusals\":%llu,\"preparation_refusals\":%llu,\"owner_cpu_observed\":%d,"
        "\"helper_cpu_requested\":%d,\"helper_cpu_observed\":%d,\"alternative_cpu_available\":%s,"
        "\"helper_thread_cpu_ns\":%llu,\"helper_cold_startup_ns\":%llu,\"helper_join_ns\":%llu,"
        "\"charged_after_drain\":0,\"performance_claim\":false}\n",
        name, inventory_limit, maximum_inventory, accepted,
        static_cast<unsigned long long>(stockouts), static_cast<unsigned long long>(ready_takes),
        static_cast<unsigned long long>(full_refusals), static_cast<unsigned long long>(stopped_refusals),
        static_cast<unsigned long long>(preparation_refusals),
        helper.owner_cpu, helper.requested_cpu, helper.observed_cpu, helper.alternative_cpu ? "true" : "false",
        static_cast<unsigned long long>(helper.thread_cpu_ns), static_cast<unsigned long long>(helper.startup_ns),
        static_cast<unsigned long long>(helper.join_ns));
  }

  ~Caller() { CHECK(finished); }
};

void ready_stockout_failure(Shape shape) {
  Caller caller(shape);
  const auto ready = required(caller.offer(7));
  auto stockout = caller.take_ready();
  CHECK(!stockout && stockout.error() == Error::unavailable);
  auto second = caller.offer(19, true);
  if (shape == Shape::finite) {
    // The first fresh palette's control preparation has not returned to its
    // owner. Refusal accepts no work; this caller chooses to wait for that
    // physical completion before making one explicit new admission attempt.
    CHECK(!second && second.error() == Error::unavailable);
    CHECK(caller.accepted == 1 && caller.inventory() == 1 && caller.preparation_refusals == 1);
    const auto& refused = caller.slots[1].preparation;
    CHECK(refused.state == loom::Preparation::State::idle && !refused.backend && !refused.owner);
    caller.settle();
    second = caller.offer(19, true);
  }
  const auto failed = required(std::move(second));
  CHECK(ready == 0 && failed == 1 && caller.inventory() == inventory_limit);
  auto full = caller.offer(31);
  CHECK(!full && full.error() == Error::capacity && caller.accepted == inventory_limit);
  caller.stop_admission();
  auto stopped = caller.offer(41);
  CHECK(!stopped && stopped.error() == Error::rejected && caller.accepted == inventory_limit);
  caller.settle();
  const auto& failure = caller.slots[failed].preparation;
  CHECK(failure.state == loom::Preparation::State::failed && failure.error == Error::io);
  CHECK(caller.slots[failed].context.checksum == padding * 0xa5);
  CHECK(failure.changed.state == orbital::Readiness::failed && !failure.value && !failure.owner);
  auto absent = loom::take(caller.slots[failed].preparation);
  CHECK(!absent && absent.error() == Error::io);
  auto allocation = required(caller.take_ready()); // Direct owner take, outside Ring/Task dispatch.
  caller.verify_and_release(allocation, caller.slots[ready].context);
  CHECK(caller.inventory() == 0 && caller.ready_takes == 1 && caller.stockouts == 1);
  caller.finish(shape == Shape::window ? "window-ready-stockout-bounded-refusal-constructor-failure"
                                      : "finite-ready-stockout-bounded-refusal-constructor-failure");
}

void unread_ready_cancel(Shape shape) {
  Caller caller(shape);
  const auto index = required(caller.offer(53));
  caller.stop_admission();
  caller.settle();
  auto& preparation = caller.slots[index].preparation;
  CHECK(preparation.state == loom::Preparation::State::ready && preparation.value);
  loom::cancel(preparation);
  CHECK(preparation.state == loom::Preparation::State::cancelled && !preparation.value && !preparation.owner);
  CHECK(caller.inventory() == 0 && caller.ready_takes == 0);
  caller.finish(shape == Shape::window ? "window-unread-ready-cancel" : "finite-unread-ready-cancel");
}

void outstanding_cancel_close(Shape shape) {
  Caller caller(shape);
  const auto index = required(caller.offer(67, false, true));
  auto& slot = caller.slots[index];
  const auto limit = deadline();
  while (!slot.context.entered.load(std::memory_order_acquire)) {
    CHECK(Clock::now() < limit);
    // Finite preparation can require an owner control completion before its
    // constructor phase is posted. Native work waits outside any Ring visit.
    loom::poll(caller.worker, {.messages = 1, .tasks = 0});
    std::this_thread::yield();
  }
  caller.stop_admission();
  const auto charged = caller.arena->charged_bytes.load();
  loom::cancel(slot.preparation);
  CHECK(slot.preparation.state == loom::Preparation::State::pending && slot.preparation.backend);
  CHECK(loom::shutdown(caller.worker).kind() == Progress::Kind::waiting);
  CHECK(caller.arena->charged_bytes.load() == charged);
  struct Completion { unsigned handled{}, released{}; } completion;
  const orbital::worker::MessageOps operations{
    [](orbital::worker::Message&) noexcept {},
    [](orbital::worker::Message& message, orbital::worker::Lane&) noexcept {
      ++static_cast<Completion*>(message.context)->handled;
    },
    [](orbital::worker::Message& message) noexcept {
      ++static_cast<Completion*>(message.context)->released;
    }};
  orbital::worker::Message message;
  message.context = &completion;
  message.ops = &operations;
  orbital::worker::post(caller.worker.lane, message);
  CHECK(loom::poll(caller.worker, {.messages = 1, .tasks = 0}) == 1);
  CHECK(completion.handled == 1 && completion.released == 1);
  CHECK(slot.preparation.state == loom::Preparation::State::pending && !slot.context.proceed.load());
  CHECK(loom::shutdown(caller.worker).kind() == Progress::Kind::waiting);
  slot.context.proceed.store(true, std::memory_order_release);
  caller.settle();
  CHECK(slot.preparation.state == loom::Preparation::State::cancelled);
  CHECK(!slot.preparation.value && !slot.preparation.owner && slot.context.checksum != 0);
  caller.finish(shape == Shape::window ? "window-outstanding-cancel-blocked-constructor-close"
                                      : "finite-outstanding-cancel-blocked-constructor-close");
}
} // namespace

int main(int argc, char** argv) {
  CHECK(argc <= 2);
  const std::string_view selected = argc == 2 ? argv[1] : "both";
  CHECK(selected == "both" || selected == "window" || selected == "finite");
  for (const auto shape : {Shape::window, Shape::finite}) {
    if (selected != "both" && (selected == "window") != (shape == Shape::window)) continue;
    ready_stockout_failure(shape);
    unread_ready_cancel(shape);
    outstanding_cancel_close(shape);
  }
  std::puts("{\"summary\":\"passed\",\"scope\":\"finite-caller-lifetime-composition\",\"performance_claim\":false}");
}
