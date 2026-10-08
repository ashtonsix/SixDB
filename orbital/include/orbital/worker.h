#pragma once

// OS-thread placement and intrusive physical-completion queues.
// Loom adds scheduling; a Lane can service Messages without Tasks.

#include <orbital/types.h>
#include <atomic>
#include <thread>

namespace orbital::worker {
/// Many threads may post; one adopted owner handles completions.
struct alignas(64) Lane {
  int cpu{-1};
  /// Install before posting; keep callback/context immutable and alive until
  /// all posting calls return. May run concurrently; bounded wake-only work
  /// must not poll, repost, hand off or tear down the Lane.
  void (*notify_posted)(void*) noexcept{};
  void* notify_context{};
  alignas(64) Message sentinel;
  alignas(64) std::atomic<Message*> producer{&sentinel};
  alignas(64) Message* consumer{&sentinel};
  std::atomic<void*> owner{};
  Lane* owner_next{};
  Lane() = default;
  ~Lane();
  Lane(const Lane&) = delete;
  Lane& operator=(const Lane&) = delete;
};

/// Handoff requires old-owner relinquishment and caller-established happens-before.
[[nodiscard]] Result<void> adopt(Lane&, int cpu = -1);
void relinquish(Lane&) noexcept;
bool on_lane(const Lane&) noexcept;
/// Record must remain stable; cannot repost while queued. Capacity cannot refuse it.
/// Capture the optional route hint before publication, then invoke it after
/// linking payload. Neither Lane nor Message is accessed after linkage. The
/// receiver may have destroyed the record by then. Notification context stays
/// alive until all post calls return, including callbacks after queue drain.
void post(Lane&, Message&) noexcept;
/// Counts handled Messages; an incomplete producer publication can yield zero
/// while pending remains true. Queue link work is bounded by message_budget.
std::uint64_t poll(Lane&, std::uint64_t message_budget) noexcept;
/// Owner query, also permitted after a joined relinquishment. Does not establish
/// that external publishers have stopped.
bool pending(const Lane&) noexcept;

struct Thread {
  std::thread native;
  Thread() = default;
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
};

/// Apply optional affinity before entry runs; -1 inherits caller affinity.
/// Setup/affinity failure is reported before return, without calling entry.
[[nodiscard]] Result<void> spawn(Thread&, void (*entry)(void*) noexcept,
    void* context, int cpu = -1);
[[nodiscard]] Result<void> join(Thread&);
} // namespace orbital::worker
