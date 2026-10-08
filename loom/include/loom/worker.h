#pragma once

// Scheduling over an Orbital Lane and allocation Pool. Ready-byte work can
// run directly; Tasks keep caller-owned state across yields and Signal waits.

#include <orbital/allocation.h>
#include <orbital/worker.h>
#include <loom/hints.h>
#include <cassert>
#include <memory>
#include <utility>

namespace loom {

using orbital::Progress;
using orbital::allocation::bytes;
using orbital::allocation::release;
// Owner-lane operations select the Pool from captured storage.
using orbital::allocation::grow;
using orbital::allocation::hold;
using orbital::allocation::retain;

struct Worker;
class HardwareProfile;
enum class TaskState { idle, running, queued, waiting, done, failed, detached };

/// Caller-owned stable frame. Detach before destroying a queued/waiting frame;
/// a terminal frame has no Worker registration. Use a fresh Task for new work.
/// Wait Signals must belong to the Worker's Lane; each step bounds its own work.
struct Task {
  Worker* worker{};
  void* context{};
  Progress (*step)(Task&) noexcept{};
  Task* next{};
  orbital::WaitLink wait;
  TaskState state{TaskState::idle};
  orbital::Error failure{orbital::Error::unavailable};
};

/// Stable Lane and local allocation Pool. The caller assigns its thread, ends
/// external posting, and drains actual storage users before destruction.
struct Worker {
  orbital::worker::Lane lane;
  orbital::Ref<orbital::allocation::Arena> arena;
  orbital::allocation::Pool* memory{};
  Task* first{};
  Task* last{};
  std::uint64_t task_limit{}, active_tasks{};
  orbital::Signal capacity_changed;
  HintPolicy hints;
  std::shared_ptr<HardwareProfile> hardware;
  bool stopping{}, polling{};

  Worker() = default;
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
};

[[nodiscard]] inline orbital::Result<void> adopt(Worker& worker, int cpu = -1) {
  return orbital::worker::adopt(worker.lane, cpu);
}

inline void relinquish(Worker& worker) noexcept {
  assert(!worker.polling);
  orbital::worker::relinquish(worker.lane);
}

/// Configure on the assigned thread. Zero Task capacity still permits direct
/// native work and physical Message service. Failure publishes no local Pool or work.
[[nodiscard]] orbital::Result<void> configure(Worker&,
    orbital::Ref<orbital::allocation::Arena>, std::uint64_t task_capacity,
    std::shared_ptr<HardwareProfile> hardware = {});

/// Owner boundary only. Geometry stays fixed; stale/invalid profiles are refused.
[[nodiscard]] orbital::Result<void> set_hint_profile(Worker&, HintProfile) noexcept;
[[nodiscard]] orbital::Result<void> set_hint_pressure(Worker&, HintPressure) noexcept;
/// Adopt a completed startup observation coarsely. Rings explicitly refresh
/// their snapshots after returning; native callbacks never query this handle.
[[nodiscard]] orbital::Result<void> refresh_hints(Worker&) noexcept;

/// Refusal changes neither frame nor Worker. Acceptance queues without running
/// inline; waiting/yielding Tasks keep the same slot until terminal or detached.
[[nodiscard]] bool start(Worker&, Task&) noexcept;

/// Returned-boundary cancellation of scheduling interest. Physical uses and
/// operation-specific Ring cleanup remain with their owners.
void detach(Task&) noexcept;

struct PollBudget { std::uint64_t messages; std::uint64_t tasks; };

/// Count-limited nonblocking service on the assigned thread; no reentrant polling.
/// A Message or Task retirement can synchronously notify all Signal waiters;
/// these budgets count visits, not that notification fanout or elapsed time.
/// Message service is independent of Task admission; zero does not prove idleness.
std::uint64_t poll(Worker&, PollBudget) noexcept;

/// Reject new work and wait for existing Tasks to finish or detach, then close
/// the Pool while polling physical returns. Completion invalidates the Pool handle;
/// copy its final statistics before then. Stop external posting and settle
/// retained backend records; an empty queue does not prove publishers have returned.
[[nodiscard]] Progress shutdown(Worker&);

/// Select the Worker's Pool once at a native operation boundary. Ikea consumes
/// the returned bytes directly, without Task dispatch or per-value checks.
[[nodiscard]] inline orbital::Result<orbital::allocation::Allocation> allocate(
    Worker& worker, const orbital::allocation::AllocationRequest& request) {
  assert(orbital::worker::on_lane(worker.lane));
  if (!worker.memory)
    return std::unexpected(orbital::Error::unavailable);
  return orbital::allocation::allocate(*worker.memory, request);
}

} // namespace loom
