#include <orbital/worker.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <source_location>
#include <thread>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace orbital::worker;
using Clock = std::chrono::steady_clock;
constexpr auto timeout = std::chrono::seconds(10);
std::atomic<std::uint64_t> checks{};
void check(bool condition, const char* reason,
    std::source_location where = std::source_location::current()) {
  checks.fetch_add(1, std::memory_order_relaxed);
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
  std::abort();
}

template<class Predicate> void await(Predicate ready, const char* reason) {
  const auto deadline = Clock::now() + timeout;
  while (!ready()) {
    check(Clock::now() < deadline, reason);
    std::this_thread::yield();
  }
}

struct Owner {
  Lane lane;
  Owner() { check(adopt(lane).has_value(), "Lane adoption failed"); }
  ~Owner() {
    check(!pending(lane), "owner destroyed a pending Message");
    relinquish(lane);
  }
};

std::uint64_t payload(unsigned producer, std::uint64_t sequence) {
  return (sequence + 1) * 0x9e3779b97f4a7c15ULL ^
      (std::uint64_t(producer) + 7) * 0xd6e8feb86659fd93ULL;
}

struct History {
  std::atomic<std::uint64_t> retained{};
  std::uint64_t handled{}, released{};
  std::array<std::uint64_t, 4> next{};
};

struct Record {
  Message message;
  History* history{};
  unsigned producer{};
  std::uint64_t sequence{}, value{};
  std::atomic<unsigned> references{};
  unsigned handled{};
  static const MessageOps operations;
  Record() { message.context = this; message.ops = &operations; }
  void prepare(History& h, unsigned p, std::uint64_t s) {
    history = &h; producer = p; sequence = s; value = payload(p, s);
  }
};

const MessageOps Record::operations{
  [](Message& message) noexcept {
    auto& record = *static_cast<Record*>(message.context);
    check(record.references.fetch_add(1, std::memory_order_relaxed) == 0,
        "record reposted before its queue release");
    record.history->retained.fetch_add(1, std::memory_order_relaxed);
  },
  [](Message& message, Lane& lane) noexcept {
    auto& record = *static_cast<Record*>(message.context);
    check(on_lane(lane), "handler ran on a foreign owner");
    check(record.references.load(std::memory_order_relaxed) == 1,
        "queue did not retain its record");
    check(!message.next.load(std::memory_order_relaxed), "removed link remained borrowed");
    check(record.value == payload(record.producer, record.sequence),
        "payload was not published before handling");
    check(record.producer < record.history->next.size(), "invalid producer");
    check(record.sequence == record.history->next[record.producer]++,
        "producer FIFO was lost or a Message was repeated");
    check(record.handled++ == 0, "record handled twice");
    ++record.history->handled;
  },
  [](Message& message) noexcept {
    auto& record = *static_cast<Record*>(message.context);
    check(record.references.fetch_sub(1, std::memory_order_relaxed) == 1,
        "queue release lost its retained record");
    ++record.history->released;
  }
};

void history_complete(const History& history, std::uint64_t expected) {
  check(history.retained.load(std::memory_order_relaxed) == expected &&
      history.handled == expected && history.released == expected,
      "retain/handle/release counts disagree");
}

void large_backlog() {
  Owner owner;
  History history;
  constexpr std::uint64_t count = 65536;
  auto records = std::make_unique<Record[]>(count);
  for (std::uint64_t i = 0; i < count; ++i) {
    records[i].prepare(history, 0, i);
    post(owner.lane, records[i].message);
  }
  check(pending(owner.lane), "large backlog was lost");
  check(poll(owner.lane, 0) == 0 && history.handled == 0,
      "zero budget handled a Message");
  for (std::uint64_t i = 0; i < count; ++i) {
    check(poll(owner.lane, 1) == 1, "budget one did not handle exactly one ready Message");
    check(history.handled == i + 1 && history.released == i + 1,
        "budget one escaped into the backlog");
  }
  check(!pending(owner.lane) && poll(owner.lane, 1) == 0,
      "drained backlog remained pending");
  history_complete(history, count);
}

void bounded_link_work() {
  Owner owner;
  History history;
  const auto page = ::sysconf(_SC_PAGESIZE);
  check(page > 0 && sizeof(Record) <= std::size_t(page), "invalid test page geometry");
  const auto length = std::size_t(page) * 3;
  auto* memory = static_cast<std::byte*>(::mmap(nullptr, length,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  check(memory != MAP_FAILED, "guard-page fixture mapping failed");
  std::array<Record*, 3> records{};
  for (unsigned i = 0; i < records.size(); ++i) {
    records[i] = std::construct_at(reinterpret_cast<Record*>(memory + i * page));
    records[i]->prepare(history, 0, i);
    post(owner.lane, records[i]->message);
  }
  // The third record is a published successor beyond this visit's one-message
  // budget. Reversing/traversing the whole batch would fault on its protected page.
  check(::mprotect(memory + 2 * page, page, PROT_NONE) == 0, "guard-page protection failed");
  check(poll(owner.lane, 1) == 1 && history.handled == 1 && history.released == 1,
      "bounded poll did not handle the first record");
  check(::mprotect(memory + 2 * page, page, PROT_READ | PROT_WRITE) == 0,
      "guard-page restoration failed");
  check(poll(owner.lane, 8) == 2 && !pending(owner.lane), "guard-page backlog did not drain");
  history_complete(history, records.size());
  for (auto* record : records) std::destroy_at(record);
  check(::munmap(memory, length) == 0, "guard-page fixture unmap failed");
}

struct RepostHistory { unsigned retained{}, handled{}, released{}, destroyed{}; std::uint64_t sum{}; };

struct RepostRecord {
  Message message;
  Lane* lane{};
  RepostHistory* history{};
  unsigned references{}, remaining{}, visits{};
  static const MessageOps operations;
  RepostRecord(Lane& owner, RepostHistory& h, unsigned count)
      : lane(&owner), history(&h), remaining(count) {
    message.context = this; message.ops = &operations;
  }
  ~RepostRecord() {
    check(references == 0 && remaining == 0, "record destroyed while still borrowed");
    ++history->destroyed;
  }
};

const MessageOps RepostRecord::operations{
  [](Message& message) noexcept {
    auto& record = *static_cast<RepostRecord*>(message.context);
    check(record.references++ == 0, "release repost retained an already queued record");
    ++record.history->retained;
  },
  [](Message& message, Lane& lane) noexcept {
    auto& record = *static_cast<RepostRecord*>(message.context);
    check(&lane == record.lane && record.references == 1 && record.remaining,
        "repost record handled outside its ownership");
    check(!message.next.load(std::memory_order_relaxed), "repost record link was not detached");
    record.history->sum += payload(0, record.visits++);
    --record.remaining;
    ++record.history->handled;
  },
  [](Message& message) noexcept {
    auto& record = *static_cast<RepostRecord*>(message.context);
    check(record.references-- == 1, "repost release lost its queue reference");
    ++record.history->released;
    if (record.remaining) post(*record.lane, message);
    else delete &record; // Must be the queue's final touch, including its link.
  }
};

void final_release_and_repost() {
  Owner owner;
  RepostHistory history;
  constexpr unsigned count = 37;
  auto* record = new RepostRecord(owner.lane, history, count);
  post(owner.lane, record->message);
  for (unsigned i = 0; i < count; ++i)
    check(poll(owner.lane, 1) == 1 && history.handled == i + 1,
        "repost escaped its owner visit budget");
  std::uint64_t expected = 0;
  for (unsigned i = 0; i < count; ++i) expected += payload(0, i);
  check(history.retained == count && history.handled == count &&
      history.released == count && history.destroyed == 1 && history.sum == expected,
      "release repost/destruction or payload oracle failed");
  check(!pending(owner.lane) && poll(owner.lane, 1) == 0,
      "queue retained a destroyed final record");
}

void concurrent_producers() {
  Owner owner;
  History history;
  constexpr unsigned producers = 4;
  constexpr std::uint64_t each = 10000;
  auto records = std::make_unique<Record[]>(producers * each);
  std::array<std::thread, producers> threads;
  std::atomic<bool> begin{};
  for (unsigned p = 0; p < producers; ++p) {
    threads[p] = std::thread([&, p] {
      while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
      for (std::uint64_t i = 0; i < each; ++i) {
        auto& record = records[p * each + i];
        record.prepare(history, p, i);
        post(owner.lane, record.message);
        if ((i & 31) == 0) std::this_thread::yield();
      }
    });
  }
  begin.store(true, std::memory_order_release);
  await([&] {
    const auto handled = poll(owner.lane, 7);
    check(handled <= 7, "concurrent polling exceeded its budget");
    return history.handled == producers * each;
  }, "concurrent producer backlog did not drain");
  for (auto& thread : threads) thread.join();
  history_complete(history, producers * each);
  for (auto next : history.next) check(next == each, "producer partition was incomplete");
  check(!pending(owner.lane), "concurrent queue retained drained work");
}

void paused_publication(bool live_predecessor) {
  Owner owner;
  History history;
  Record predecessor, paused, successor;
  const std::uint64_t offset = live_predecessor ? 1 : 0;
  predecessor.prepare(history, 0, 0);
  paused.prepare(history, 0, offset);
  successor.prepare(history, 0, offset + 1);
  if (live_predecessor) post(owner.lane, predecessor.message);
  std::atomic<bool> exchanged{}, resume{};
  std::thread publisher([&] {
    // Stage one actual retained publisher at the exchange-to-link boundary,
    // without a production hook. All other publication and consumption below
    // use the native post/poll APIs and their real shared links.
    paused.message.ops->retain(paused.message);
    paused.message.next.store(nullptr, std::memory_order_relaxed);
    auto* prior = owner.lane.producer.exchange(&paused.message, std::memory_order_acq_rel);
    exchanged.store(true, std::memory_order_release);
    while (!resume.load(std::memory_order_acquire)) std::this_thread::yield();
    prior->next.store(&paused.message, std::memory_order_release);
  });
  await([&] { return exchanged.load(std::memory_order_acquire); }, "publisher did not enter its publication gap");
  post(owner.lane, successor.message);
  check(pending(owner.lane), "publication gap was reported idle");
  std::atomic<bool> returned{}, rescued{};
  std::thread watchdog([&] {
    const auto deadline = Clock::now() + timeout;
    while (!returned.load(std::memory_order_acquire)) {
      if (Clock::now() >= deadline) {
        rescued.store(true, std::memory_order_release);
        resume.store(true, std::memory_order_release);
        return;
      }
      std::this_thread::yield();
    }
  });
  const auto handled = poll(owner.lane, 1);
  returned.store(true, std::memory_order_release);
  watchdog.join();
  check(!rescued.load(std::memory_order_acquire), "owner waited for a preempted publisher");
  check(handled == 0 && history.handled == 0 && history.released == 0 && pending(owner.lane),
      "incomplete FIFO prefix was consumed or lost");
  check(!resume.load(std::memory_order_acquire), "publisher resumed before the owner returned");
  resume.store(true, std::memory_order_release);
  publisher.join();
  check(poll(owner.lane, 8) == offset + 2 && !pending(owner.lane),
      "resumed publication did not drain its exact FIFO prefix");
  history_complete(history, offset + 2);
}
} // namespace

int main() {
  large_backlog();
  bounded_link_work();
  final_release_and_repost();
  concurrent_producers();
  paused_publication(false);
  paused_publication(true);
  std::printf("bounded Message FIFO: %llu checks\n",
      static_cast<unsigned long long>(checks.load(std::memory_order_relaxed)));
}
