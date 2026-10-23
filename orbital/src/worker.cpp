#include <orbital/worker.h>
#include <cassert>
#include <memory>
#include <sched.h>
#include <system_error>
#include <utility>

namespace orbital::worker {
namespace {
struct Association {
  Lane* first{};
  ~Association() {
    while (first) {
      auto* lane = first;
      first = lane->owner_next;
      lane->owner_next = nullptr;
      lane->owner.store(nullptr, std::memory_order_release);
    }
  }
};

thread_local Association association;
} // namespace
namespace detail { constinit thread_local char lane_token{}; }
namespace {
// Registering the Association's destructor stays on adopt, off the owner check.
void* token() noexcept { return &detail::lane_token; }

Result<void> affinity(int cpu) {
  if (cpu < -1 || cpu >= CPU_SETSIZE) return std::unexpected(Error::invalid_argument);
  if (cpu < 0) return {};
  // A child inherits its parent's current pin, not the full cpuset permission.
  // Let the kernel enforce permission when assigning the requested CPU.
  cpu_set_t selected;
  CPU_ZERO(&selected);
  CPU_SET(cpu, &selected);
  if (::sched_setaffinity(0, sizeof(selected), &selected) != 0)
    return std::unexpected(Error::unavailable);
  return {};
}

void publish(Lane& lane, Message& message) noexcept {
  message.next.store(nullptr, std::memory_order_relaxed);
  auto* predecessor = lane.producer.exchange(&message, std::memory_order_acq_rel);
  // The successor link publishes the payload. This is the producer's final
  // predecessor touch; the owner may reclaim it after acquiring this link.
  predecessor->next.store(&message, std::memory_order_release);
}

Message* pop(Lane& lane) noexcept {
  auto* message = lane.consumer;
  auto* successor = message->next.load(std::memory_order_acquire);
  if (message == &lane.sentinel) {
    if (!successor) return nullptr;
    lane.consumer = message = successor;
    successor = message->next.load(std::memory_order_acquire);
  }
  if (!successor) {
    // A publisher may have exchanged the endpoint but not linked its
    // predecessor. Never wait for it or release its still-borrowed predecessor.
    if (lane.producer.load(std::memory_order_acquire) != message) return nullptr;
    // Detach the final real node using a stable successor before its release.
    // If a publisher races this insertion, retain the FIFO prefix until linked.
    publish(lane, lane.sentinel);
    successor = message->next.load(std::memory_order_acquire);
    if (!successor) return nullptr;
  }
  lane.consumer = successor;
  return message;
}
}

Lane::~Lane() {
  assert(!pending(*this));
  if (on_lane(*this)) relinquish(*this);
  assert(!owner.load(std::memory_order_relaxed));
}

Result<void> adopt(Lane& lane, int cpu) {
  if (lane.owner.load(std::memory_order_acquire)) return std::unexpected(Error::conflict);
  if (auto result = affinity(cpu); !result) return result;
  void* expected = nullptr;
  if (!lane.owner.compare_exchange_strong(expected, token(), std::memory_order_acq_rel))
    return std::unexpected(Error::conflict);
  lane.cpu = cpu;
  lane.owner_next = association.first;
  association.first = &lane;
  return {};
}

void relinquish(Lane& lane) noexcept {
  assert(on_lane(lane));
  auto** link = &association.first;
  while (*link != &lane) { assert(*link); link = &(*link)->owner_next; }
  *link = lane.owner_next;
  lane.owner_next = nullptr;
  lane.owner.store(nullptr, std::memory_order_release);
}

void post(Lane& lane, Message& message) noexcept {
  assert(message.ops && message.ops->retain && message.ops->handle && message.ops->release);
  message.ops->retain(message);
  const auto notify = lane.notify_posted;
  void* const context = notify ? lane.notify_context : nullptr;
  publish(lane, message);
  // Publication is the final Lane/record touch. The consumer may already have
  // released or reposted the Message; only captured notification values remain.
  if (notify) notify(context);
}

std::uint64_t poll(Lane& lane, std::uint64_t budget) noexcept {
  assert(on_lane(lane));
  std::uint64_t handled = 0;
  while (handled < budget) {
    auto* message = pop(lane);
    if (!message) break;
    message->next.store(nullptr, std::memory_order_relaxed);
    const auto* ops = message->ops;
    ops->handle(*message, lane);
    ++handled;
    // Final queue touch. Release may destroy or repost the reserved record.
    ops->release(*message);
  }
  return handled;
}

bool pending(const Lane& lane) noexcept {
  return lane.consumer != &lane.sentinel ||
      lane.sentinel.next.load(std::memory_order_acquire) ||
      lane.producer.load(std::memory_order_acquire) != &lane.sentinel;
}

Result<void> spawn(Thread& thread, void (*entry)(void*) noexcept, void* context, int cpu) {
  if (!entry || thread.native.joinable()) return std::unexpected(Error::invalid_argument);
  if (cpu < -1 || cpu >= CPU_SETSIZE) return std::unexpected(Error::invalid_argument);
  // Thread setup and affinity failure are reported before its user callback runs.
  std::shared_ptr<std::atomic<int>> ready;
  try {
    ready = std::make_shared<std::atomic<int>>(0);
    thread.native = std::thread([=] {
      const bool accepted = affinity(cpu).has_value();
      ready->store(accepted ? 1 : -1, std::memory_order_release);
      ready->notify_one();
      if (accepted) entry(context);
    });
  } catch (const std::system_error&) { return std::unexpected(Error::unavailable); }
    catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
  int state;
  while (!(state = ready->load(std::memory_order_acquire))) ready->wait(0, std::memory_order_acquire);
  if (state < 0) { thread.native.join(); return std::unexpected(Error::unavailable); }
  return {};
}

Result<void> join(Thread& thread) {
  if (!thread.native.joinable() || thread.native.get_id() == std::this_thread::get_id())
    return std::unexpected(Error::invalid_argument);
  try { thread.native.join(); }
  catch (const std::system_error&) { return std::unexpected(Error::unavailable); }
  return {};
}
} // namespace orbital::worker
