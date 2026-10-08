#include <loom/worker.h>
#include <loom/hardware.h>
#include <unistd.h>

namespace loom {
namespace {

std::uint64_t service(Worker& worker, std::uint64_t budget) noexcept {
  if (!budget || !orbital::worker::pending(worker.lane)) return 0;
  return orbital::worker::poll(worker.lane, budget);
}

void queue(Worker& worker, Task& task) noexcept {
  assert(!task.next && !task.wait.source);
  task.state = TaskState::queued;
  if (worker.last) worker.last->next = &task;
  else worker.first = &task;
  worker.last = &task;
}

void wake(void* context) noexcept {
  auto& task = *static_cast<Task*>(context);
  assert(task.worker && task.state == TaskState::waiting && !task.wait.source);
  queue(*task.worker, task);
}

void unlink_wait(Task& task) noexcept {
  if (!task.wait.source) return;
  auto** link = &task.wait.source->waiters;
  while (*link != &task.wait) {
    assert(*link);
    link = &(*link)->next;
  }
  *link = task.wait.next;
  task.wait.source = nullptr;
  task.wait.next = nullptr;
}

void retire(Task& task, TaskState terminal) noexcept {
  auto& worker = *task.worker;
  assert(!task.next && !task.wait.source && worker.active_tasks);
  task.state = terminal;
  task.worker = nullptr;
  task.wait.waiter = nullptr;
  task.wait.notify = nullptr;
  --worker.active_tasks;
  orbital::pulse(worker.capacity_changed);
}

void park(Task& task, Progress progress) noexcept {
  auto& worker = *task.worker;
  auto* source = progress.signal();
  if (source->lane != &worker.lane) {
    task.failure = orbital::Error::invalid_argument;
    retire(task, TaskState::failed);
    return;
  }
  if (source->generation != progress.observed_generation() ||
      source->state == orbital::Readiness::ready ||
      source->state == orbital::Readiness::failed ||
      source->state == orbital::Readiness::cancelled) {
    queue(worker, task);
    return;
  }
  task.state = TaskState::waiting;
  task.wait.source = source;
  task.wait.next = source->waiters;
  task.wait.waiter = &task;
  task.wait.notify = wake;
  source->waiters = &task.wait;
  // All Signal mutations occur on this Lane; no call intervenes between the
  // observation check and enrollment. Backend threads post Messages instead.
}

} // namespace

Worker::~Worker() {
  assert(!memory && !arena && !first && !last && !active_tasks && !polling);
  assert(!capacity_changed.waiters);
  assert(!orbital::worker::pending(lane));
  assert(!orbital::worker::on_lane(lane));
}

orbital::Result<void> configure(Worker& worker,
    orbital::Ref<orbital::allocation::Arena> arena, std::uint64_t task_capacity,
    std::shared_ptr<HardwareProfile> hardware) {
  assert(orbital::worker::on_lane(worker.lane));
  if (!arena || worker.memory || worker.stopping)
    return std::unexpected(orbital::Error::invalid_argument);
  auto reported = reported_hint_geometry(worker.lane.cpu);
  HintGeometry geometry;
  if (reported) geometry = *reported;
  else {
    const auto page = ::sysconf(_SC_PAGESIZE);
    if (page > 0) geometry.page_bytes = static_cast<std::size_t>(page);
  }
  HintProfile profile{0, geometry, 8, 0};
  if (hardware) {
    auto discovered = discover_hardware(worker.lane.cpu);
    profile = hardware->snapshot().profile;
    if (!valid_hint_profile(profile) || profile.geometry != geometry ||
        !discovered || !compatible_hardware_context(hardware->context(), *discovered))
      return std::unexpected(orbital::Error::invalid_argument);
  }
  if (!valid_hint_profile(profile)) return std::unexpected(orbital::Error::invalid_argument);
  auto pool = orbital::allocation::add_pool(*arena, worker.lane);
  if (!pool) return std::unexpected(pool.error());
  worker.arena = std::move(arena);
  worker.memory = *pool;
  worker.task_limit = task_capacity;
  worker.capacity_changed.lane = &worker.lane;
  worker.hints = make_hint_policy(profile, HintPressure::low);
  worker.hardware = std::move(hardware);
  return {};
}

orbital::Result<void> set_hint_profile(Worker& worker, HintProfile profile) noexcept {
  if (!orbital::worker::on_lane(worker.lane) || !worker.memory)
    return std::unexpected(orbital::Error::conflict);
  if (!valid_hint_profile(profile) || profile.geometry != worker.hints.profile.geometry)
    return std::unexpected(orbital::Error::invalid_argument);
  if (profile.revision <= worker.hints.profile.revision)
    return std::unexpected(orbital::Error::conflict);
  worker.hints = make_hint_policy(profile, worker.hints.pressure);
  return {};
}
orbital::Result<void> set_hint_pressure(Worker& worker, HintPressure pressure) noexcept {
  if (!orbital::worker::on_lane(worker.lane) || !worker.memory)
    return std::unexpected(orbital::Error::conflict);
  if (unsigned(pressure) > 1) return std::unexpected(orbital::Error::invalid_argument);
  worker.hints = make_hint_policy(worker.hints.profile, pressure);
  return {};
}
orbital::Result<void> refresh_hints(Worker& worker) noexcept {
  if (!orbital::worker::on_lane(worker.lane) || !worker.memory)
    return std::unexpected(orbital::Error::conflict);
  if (!worker.hardware) return {};
  const auto profile = worker.hardware->snapshot().profile;
  if (profile.revision <= worker.hints.profile.revision) return {};
  return set_hint_profile(worker, profile);
}

bool start(Worker& worker, Task& task) noexcept {
  assert(orbital::worker::on_lane(worker.lane));
  if (!worker.memory || worker.stopping || worker.active_tasks == worker.task_limit ||
      task.state != TaskState::idle || !task.step || task.worker ||
      task.next || task.wait.source)
    return false;
  task.worker = &worker;
  ++worker.active_tasks;
  queue(worker, task);
  return true;
}

void detach(Task& task) noexcept {
  if (!task.worker) return;
  auto& worker = *task.worker;
  assert(orbital::worker::on_lane(worker.lane));
  assert(task.state != TaskState::running);
  if (task.state == TaskState::queued) {
    Task* previous = nullptr;
    auto** link = &worker.first;
    while (*link != &task) {
      assert(*link);
      previous = *link;
      link = &(*link)->next;
    }
    *link = task.next;
    if (worker.last == &task) worker.last = previous;
    task.next = nullptr;
  } else {
    assert(task.state == TaskState::waiting);
    unlink_wait(task);
  }
  retire(task, TaskState::detached);
}

std::uint64_t poll(Worker& worker, PollBudget budget) noexcept {
  assert(orbital::worker::on_lane(worker.lane) && !worker.polling);
  worker.polling = true;
  auto messages = service(worker, budget.messages);
  std::uint64_t tasks = 0;
  while (tasks != budget.tasks && worker.first) {
    auto& task = *worker.first;
    worker.first = task.next;
    if (!worker.first) worker.last = nullptr;
    task.next = nullptr;
    task.state = TaskState::running;
    const auto progress = task.step(task);
    ++tasks;
    switch (progress.kind()) {
      case Progress::Kind::done: retire(task, TaskState::done); break;
      case Progress::Kind::failed:
        task.failure = progress.failure();
        retire(task, TaskState::failed);
        break;
      case Progress::Kind::yielded: queue(worker, task); break;
      case Progress::Kind::waiting: park(task, progress); break;
    }
    if (messages != budget.messages)
      messages += service(worker, budget.messages - messages);
  }
  worker.polling = false;
  return messages + tasks;
}

Progress shutdown(Worker& worker) {
  assert(orbital::worker::on_lane(worker.lane) && !worker.polling);
  worker.stopping = true;
  if (worker.active_tasks) return Progress::wait(worker.capacity_changed);
  if (orbital::worker::pending(worker.lane))
    return Progress::yield();
  if (!worker.memory) return Progress::done();
  auto progress = orbital::allocation::close(*worker.memory);
  if (progress.kind() != Progress::Kind::done) return progress;
  auto removed = orbital::allocation::remove_pool(*worker.memory);
  if (!removed) return Progress::fail(removed.error());
  worker.memory = nullptr;
  worker.hardware.reset();
  worker.arena = {};
  worker.task_limit = 0;
  if (orbital::worker::pending(worker.lane))
    return Progress::yield();
  return Progress::done();
}

} // namespace loom
