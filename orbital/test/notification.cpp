#include <orbital/worker.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <poll.h>
#include <source_location>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace orbital::worker;
using Clock = std::chrono::steady_clock;
constexpr auto timeout = std::chrono::seconds(10);
std::atomic<std::uint64_t> checks{};
void check(bool valid, const char* reason,
    std::source_location where = std::source_location::current()) noexcept {
  checks.fetch_add(1, std::memory_order_relaxed);
  if (valid) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
  std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template<class Predicate> void await(Predicate ready, const char* reason) {
  const auto deadline = Clock::now() + timeout;
  while (!ready()) {
    check(Clock::now() < deadline, reason);
    std::this_thread::yield();
  }
}

// Wake state outlives every publishing call and the receiver's stop/join.
struct Wake {
  std::mutex mutex;
  std::condition_variable changed;
  const int event{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
  std::atomic<std::uint64_t> epoch{}, calls{}, returned{}, active{};
  std::uint64_t sleeps{};
  bool started{}, waiting{}, stop{}, block{}, allow{};
  Wake() { CHECK(event >= 0); }
  void signal() noexcept {
    const std::uint64_t one = 1;
    const auto n = ::write(event, &one, sizeof(one));
    CHECK(n == static_cast<ssize_t>(sizeof(one)) || (n == -1 && errno == EAGAIN));
  }
  void drain_events() noexcept {
    std::uint64_t count;
    while (::read(event, &count, sizeof(count)) == static_cast<ssize_t>(sizeof(count))) {}
    CHECK(errno == EAGAIN);
  }
  static void posted(void* pointer) noexcept {
    auto& self = *static_cast<Wake*>(pointer);
    ++self.calls; ++self.active; ++self.epoch;
    self.signal();
    self.changed.notify_all();
    if (self.block) {
      // Test-only gate forces record destruction before callback return.
      // Production callbacks must do bounded nonblocking wake-only work.
      std::unique_lock lock(self.mutex);
      CHECK(self.changed.wait_until(lock, Clock::now() + timeout, [&] { return self.allow; }));
    }
    --self.active; ++self.returned;
    self.changed.notify_all();
  }
  template<class Predicate> void until(Predicate predicate) {
    std::unique_lock lock(mutex);
    CHECK(changed.wait_until(lock, Clock::now() + timeout, predicate));
  }
  ~Wake() { CHECK(active == 0 && calls.load() == returned.load()); CHECK(::close(event) == 0); }
};

std::uint64_t payload(unsigned producer, std::uint64_t sequence) {
  return (sequence + 1) * 0x9e3779b97f4a7c15ULL ^
      (std::uint64_t(producer) + 7) * 0xd6e8feb86659fd93ULL;
}

struct History {
  Wake* wake{};
  std::array<std::uint64_t, 4> limits{}, next{};
  std::atomic<std::uint64_t> retained{}, handled{}, released{}, destroyed{};
  std::uint64_t sum{};
};

struct Record {
  Message message;
  History& history;
  Lane& lane;
  unsigned producer;
  std::uint64_t sequence, value, remaining;
  std::atomic<unsigned> references{};
  static const MessageOps operations;
  Record(History& h, Lane& l, unsigned p, std::uint64_t s, std::uint64_t uses = 1)
      : history(h), lane(l), producer(p), sequence(s), value(payload(p, s)), remaining(uses) {
    message.context = this; message.ops = &operations;
  }
  ~Record() {
    CHECK(references.load(std::memory_order_relaxed) == 0 && remaining == 0);
    history.destroyed.fetch_add(1, std::memory_order_release);
    if (history.wake) history.wake->changed.notify_all();
  }
};

const MessageOps Record::operations{
  [](Message& message) noexcept {
    auto& record = *static_cast<Record*>(message.context);
    CHECK(record.references.fetch_add(1, std::memory_order_relaxed) == 0);
    record.history.retained.fetch_add(1, std::memory_order_relaxed);
  },
  [](Message& message, Lane& lane) noexcept {
    auto& record = *static_cast<Record*>(message.context);
    CHECK(on_lane(lane) && &lane == &record.lane);
    CHECK(record.references.load(std::memory_order_relaxed) == 1 && record.remaining);
    CHECK(!message.next.load(std::memory_order_relaxed));
    CHECK(record.producer < record.history.next.size());
    CHECK(record.sequence == record.history.next[record.producer]);
    CHECK(record.sequence < record.history.limits[record.producer]);
    CHECK(record.value == payload(record.producer, record.sequence));
    ++record.history.next[record.producer];
    record.history.sum += record.value;
    --record.remaining;
    record.history.handled.fetch_add(1, std::memory_order_release);
  },
  [](Message& message) noexcept {
    auto& record = *static_cast<Record*>(message.context);
    CHECK(record.references.fetch_sub(1, std::memory_order_relaxed) == 1);
    record.history.released.fetch_add(1, std::memory_order_release);
    if (record.remaining) {
      ++record.sequence;
      record.value = payload(record.producer, record.sequence);
      post(record.lane, record.message);
    } else delete &record; // No Message/record access follows physical release.
  }
};

void complete(const History& history, std::uint64_t record_count) {
  std::uint64_t uses = 0, sum = 0;
  for (unsigned producer = 0; producer < history.limits.size(); ++producer) {
    CHECK(history.next[producer] == history.limits[producer]);
    uses += history.limits[producer];
    for (std::uint64_t i = 0; i < history.limits[producer]; ++i) sum += payload(producer, i);
  }
  CHECK(history.retained == uses && history.handled == uses && history.released == uses);
  CHECK(history.destroyed == record_count && history.sum == sum);
}

struct Receiver {
  Wake& wake;
  Lane lane;
  std::thread thread;
  explicit Receiver(Wake& w) : wake(w) {
    lane.notify_posted = Wake::posted;
    lane.notify_context = &wake;
    thread = std::thread([this] { run(); });
    wake.until([&] { return wake.started && wake.waiting; });
  }
  void run() {
    CHECK(adopt(lane));
    {
      std::lock_guard lock(wake.mutex);
      wake.started = true;
      wake.changed.notify_all();
    }
    for (;;) {
      wake.drain_events();
      const auto observed = wake.epoch.load(std::memory_order_acquire);
      while (poll(lane, 1)) {}
      std::unique_lock lock(wake.mutex);
      if (wake.stop) {
        CHECK(!pending(lane));
        break;
      }
      if (wake.epoch != observed) continue;
      wake.waiting = true; ++wake.sleeps;
      wake.changed.notify_all();
      lock.unlock();
      pollfd descriptor{wake.event, POLLIN, 0};
      int ready;
      do { ready = ::poll(&descriptor, 1, 10000); } while (ready < 0 && errno == EINTR);
      CHECK(ready == 1 && (descriptor.revents & POLLIN));
      lock.lock();
      wake.waiting = false;
    }
    relinquish(lane);
  }
  void stop_join(std::uint64_t calls) {
    {
      std::lock_guard lock(wake.mutex);
      // All external publishers have joined. Queue drain alone cannot prove
      // callback completion or permit destroying their notification context.
      CHECK(wake.calls == calls && wake.returned == calls && !wake.active);
      wake.stop = true;
      wake.signal();
    }
    thread.join();
    CHECK(!pending(lane));
  }
  ~Receiver() { CHECK(!thread.joinable()); }
};

void unnotified() {
  Lane lane;
  CHECK(!lane.notify_posted && !lane.notify_context);
  CHECK(adopt(lane));
  History history;
  history.limits[0] = 32;
  for (std::uint64_t sequence = 0; sequence < 32; ++sequence) {
    auto* record = new Record(history, lane, 0, sequence);
    post(lane, record->message);
  }
  CHECK(pending(lane) && poll(lane, 0) == 0 && history.handled == 0);
  for (std::uint64_t i = 0; i < 32; ++i) CHECK(poll(lane, 1) == 1 && history.handled == i + 1);
  CHECK(!pending(lane) && !poll(lane, 1));
  complete(history, 32);
  relinquish(lane);
  std::puts("case=default-unnotified messages=32 wakes=0 verified=1");
}

void sleeping_phases() {
  Wake wake;
  History history; history.wake = &wake;
  constexpr unsigned phases = 8, producers = 4, per_phase = 8;
  history.limits.fill(phases * per_phase);
  Receiver receiver(wake);
  for (unsigned phase = 0; phase < phases; ++phase) {
    wake.until([&] { return wake.waiting; });
    std::array<std::thread, producers> publishers;
    for (unsigned producer = 0; producer < producers; ++producer) {
      publishers[producer] = std::thread([&, producer] {
        for (unsigned i = 0; i < per_phase; ++i) {
          auto* record = new Record(history, receiver.lane, producer, phase * per_phase + i);
          post(receiver.lane, record->message);
        }
      });
    }
    for (auto& publisher : publishers) publisher.join();
    await([&] { return history.destroyed.load(std::memory_order_acquire) ==
        std::uint64_t(phase + 1) * producers * per_phase; }, "sleeping receiver lost a published phase");
    wake.until([&] { return wake.waiting; });
  }
  receiver.stop_join(phases * producers * per_phase);
  complete(history, phases * producers * per_phase);
  CHECK(wake.sleeps >= phases);
  std::puts("case=sleeping-receiver phases=8 producers=4 messages=256 producer_order=1 exactly_once=1 verified=1");
}

void destroy_before_wake_returns() {
  Wake wake; wake.block = true;
  History history; history.wake = &wake; history.limits[0] = 1;
  Receiver receiver(wake);
  std::atomic<bool> post_returned{};
  std::thread publisher([&] {
    auto* record = new Record(history, receiver.lane, 0, 0);
    post(receiver.lane, record->message);
    post_returned.store(true, std::memory_order_release);
  });
  await([&] { return wake.active.load(std::memory_order_acquire) == 1; }, "wake callback did not begin");
  await([&] { return history.destroyed.load(std::memory_order_acquire) == 1; }, "record did not retire while callback was blocked");
  {
    std::lock_guard lock(wake.mutex);
    CHECK(wake.active == 1 && wake.returned == 0 && !post_returned.load(std::memory_order_acquire));
    wake.allow = true;
    wake.changed.notify_all();
  }
  publisher.join();
  CHECK(post_returned.load(std::memory_order_acquire));
  receiver.stop_join(1);
  complete(history, 1);
  std::puts("case=destroy-before-wake-return destroyed_before_callback_end=1 publisher_joined_before_stop=1 verified=1");
}

void release_reposts() {
  Wake wake;
  History history; history.wake = &wake; history.limits[0] = 37;
  Receiver receiver(wake);
  auto* record = new Record(history, receiver.lane, 0, 0, 37);
  post(receiver.lane, record->message);
  await([&] { return history.destroyed.load(std::memory_order_acquire) == 1; }, "release repost did not drain");
  // The initial external post has returned; owner-side repost calls finish
  // before their handlers complete. Wake state survives the receiver join.
  wake.until([&] { return wake.returned == 37 && !wake.active; });
  receiver.stop_join(37);
  complete(history, 1);
  std::puts("case=final-release-repost posts=37 retained=37 handled=37 released=37 destroyed=1 verified=1");
}
} // namespace

int main() {
  static_assert(offsetof(Lane, notify_posted) == 8 && offsetof(Lane, notify_context) == 16);
  static_assert(offsetof(Lane, sentinel) == 64);
  unnotified();
  sleeping_phases();
  destroy_before_wake_returns();
  release_reposts();
  std::printf("summary=passed lane_bytes=%zu notify_offset=%zu context_offset=%zu checks=%llu performance_claim=0\n",
      sizeof(Lane), offsetof(Lane, notify_posted), offsetof(Lane, notify_context),
      static_cast<unsigned long long>(checks.load(std::memory_order_relaxed)));
}
