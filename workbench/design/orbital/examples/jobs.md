# Work that waits

Ready work calls native functions directly. These examples opt into Loom's
slower scheduler because bytes or publication resources may be missing.
Each starts once, returns control to its assigned thread, and resumes on later
polls. The C++ blocks form one translation unit, in reading order.

## Read a cold byte

Keep a stable Task and cold-read state for the requested range and preparation.
Reserve request records and their range/result storage before starting. The
request-slot array stays alive through preparation; the allocation Pool
outlives the job. The frame cannot move while it is active.

```cpp
#include <loom/worker.h>
#include <orbital/journal.h>
#include <utility>

namespace candidate {
using namespace orbital;

struct ColdRead {
  ByteRange range{};
  object::ViewRequest pending;
  object::View view;
  Result<std::byte> result{std::unexpected(Error::unavailable)};
};
```

For this example, the owner's byte-array layout is one unencoded immutable
Source. Its resolved populate function can bind that memory directly. The
layout check is owner knowledge; memory residency alone is insufficient.
Capture this function with the reconstruction recipe. This tiny populator
propagates rejection; a real owner can reconstruct incompatible material into
owned backing.

```cpp
constexpr ProgramId byte_array_layout{0, 1};

Progress populate_byte_array(const object::Material& material,
    object::ViewRequest& pending, allocation::Pool&) {
  if (material.format != byte_array_layout || material.sources.size() != 1 ||
      !material.metadata.empty() || !material.dependencies.empty())
    return Progress::fail(Error::invalid_argument);
  auto bound = object::bind_native(pending, material.sources.front());
  return bound ? Progress::done() : Progress::fail(bound.error());
}
```

Give the task its continuation. Advance preserves preparation
progress across waits. Only when the view is ready do we read the byte and close
it. Pending metadata alone does not pin resident target memory. An actual
prepared transfer owns its captured target, including during preaccept pressure.
If placement changes, preparation retries the same material after safely dealing
with old request slots; it never reselects the value from a newer State. Returning
lets the thread do other work.

```cpp
Progress advance_read(loom::Task& task) noexcept {
  auto& read = *static_cast<ColdRead*>(task.context);
  auto progress = object::advance(read.pending, read.view, *task.worker->memory);
  if (progress.kind() != Progress::Kind::done) return progress;
  auto bytes = object::read(read.view, read.range);
  read.result = bytes ? Result<std::byte>{bytes->front()} :
      Result<std::byte>{std::unexpected(bytes.error())};
  object::release(read.view, {.temperature = allocation::Temperature::hot});
  return read.result ? Progress::done() : Progress::fail(read.result.error());
}
```

Capture the selected Material, version, Buffer identity, byte bounds and access rights.
This byte-only reader needs no other interpretation metadata or full historical
State. A real data-unit reader copies any decoding metadata needed after setup.
The range and request-slot array stay at their addresses through preparation.

An already accepted parent can change its continuation here after ending its
native phase. This uses the same Task slot; spawning another Task could deadlock
when all accepted parents are waiting for additional task capacity. On failure,
the parent keeps its original continuation and handles the error at that boundary.
The parent Task and cold state are explicit, stable caller-owned objects.

```cpp
Result<void> continue_read(loom::Task& parent, ColdRead& read, object::BufferCache& cache,
    const journal::ReadContext& context,
    object::Selection selected, ByteRange range,
    std::span<Ref<port::Request>> requests) {
  if (range.size != 1) return std::unexpected(Error::invalid_argument);
  read.range = range;
  if (cache.lane != &parent.worker->lane)
    return std::unexpected(Error::invalid_argument);
  auto begun = object::begin(read.pending, context, selected, cache,
      {{&read.range, 1}, {}}, requests);
  if (!begun) return begun;
  parent.context = &read;
  parent.step = advance_read;
  return {};
}
```

For a standalone cold read, start a fresh frame on the Worker's assigned thread.
Start only queues, so this uninterrupted call on the assigned thread finishes setup before any
dispatch. Refusal reserves no resources; setup failure detaches the accepted
frame. No called owner operation recursively polls the Worker during setup.
After success the caller may close its context; a larger query keeps it for
further lookups.

```cpp
Result<void> start_read(loom::Task& task, ColdRead& read,
    const journal::ReadContext& context,
    object::Selection selected, ByteRange range, loom::Worker& worker,
    object::BufferCache& cache,
    std::span<Ref<port::Request>> requests) {
  task.context = &read;
  task.step = advance_read;
  if (!loom::start(worker, task)) return std::unexpected(Error::capacity);
  auto begun = continue_read(task, read, cache, context, selected, range, requests);
  if (!begun) loom::detach(task);
  return begun;
}
```

Later on that same thread, poll bounded work and inspect the accepted task.
`unavailable` means this visit has no value yet. A preparation failure lives
in `task.failure`: it may happen before `read.result` is written.

```cpp
Result<std::byte> visit_read(loom::Worker& worker, loom::Task& task, ColdRead& read) {
  loom::poll(worker, {.messages = 32, .tasks = 8});
  if (task.state != loom::TaskState::done &&
      task.state != loom::TaskState::failed)
    return std::unexpected(Error::unavailable);
  auto result = task.state == loom::TaskState::done ? read.result :
      Result<std::byte>{std::unexpected(task.failure)};
  loom::detach(task);
  object::release(read.view);
  object::cancel(read.pending);
  return result;
}
```

After terminal cleanup, destroy the frame. To abandon a pending read, perform
the same cleanup on its assigned thread before destruction. Accepted requests
keep their own storage and physical holds through physical completion and completion-message handling; cancelling the task does not reclaim those bytes.

## Execute an admitted epoch

First store input and replay code. Embedded code can call
`append_ready(producer, memory_log, event)` directly: MemoryLog owns the retained
events and lookup index, with capacity reserved before append. Journal then
forms an ordered batch and admits it. Stronger storage/transport providers
store input through their actual backend interfaces. Stored input, admission
and published state are distinct facts.

This example begins at the AdmittedBatch. One Draft retains the admitted input
and complete previous State while the owner keeps its actual execution cursor.
Engine/Ikea supplies `advance_owner` as an ordinary function that applies data
operations and maintenance.

It can run native work directly or drive an operation-private memory ring for
a bounded quantum, as in [native batches](native.md). A returned Task yield is
a return to the scheduler, not one memory access. Port/output waits preserve
the owner's cursor and actual outstanding uses; they do not require a generic
wait branch or Task in every ring slot.

The owner can qualify and partition native regions, pass them to ordinary
threads, then join their actual accesses before capture or freeze. Keep the
views, footprint arrays and authority stable through that join. Physical stores
must compose safely, including packed stores that preserve neighbours. This
needs no isolated Executor when the lifetime remains within the owner scope.

```cpp
struct EpochJob {
  loom::Task task;
  journal::Consumer* consumer{};
  object::Draft draft;
  std::size_t next_event{};
  std::span<const journal::EventResult> results;
  const journal::ObligationChanges* obligations{};
  Ref<journal::State> next;
  bool owner_done{};
};

Progress advance_owner(EpochJob&) noexcept;
```

The simple embedded case executes and publishes on one lane. Start a fresh
frame once on the assigned thread. Accept its Task before acquiring execution
resources; complete setup before polling. This setup is not repeated after waits.

```cpp
Result<void> start_epoch(EpochJob& job, journal::Consumer& consumer,
    const journal::AdmittedBatch& admitted, loom::Worker& worker) {
  if (&worker.lane != consumer.lane)
    return std::unexpected(Error::invalid_argument);
  job.consumer = &consumer;
  job.task.context = &job;
```

Run owner work until complete or a real wait. The owner advances its cursor
only at completed boundaries, and supplies finished results and logical
continuations in the obligation updates. Partial native stores need repair or discard.

Freeze the private object result, then prepare the actual next State.
Preparing the next State can wait. Successful final preflight and publication share one
uninterrupted call on the publishing thread; publication performs no new fallible work.

```cpp
  job.task.step = [](loom::Task& task) noexcept {
    auto& job = *static_cast<EpochJob*>(task.context);
    if (!job.owner_done) {
      auto progress = advance_owner(job);
      if (progress.kind() != Progress::Kind::done) return progress;
      job.owner_done = true;
    }
    if (!job.draft.sealed) {
      auto progress = object::freeze(job.draft);
      if (progress.kind() != Progress::Kind::done) return progress;
    }
    auto progress = journal::prepare(*job.consumer, job.draft,
        job.results, job.next, job.obligations);
    if (progress.kind() != Progress::Kind::done) return progress;
    journal::publish(*job.consumer, job.draft, std::move(job.next));
    return Progress::done();
  };
  if (!loom::start(worker, job.task)) return std::unexpected(Error::capacity);
  auto begun = journal::begin(consumer, admitted, worker.lane, job.draft);
  if (!begun) {
    loom::detach(job.task);
    return begun;
  }
  return {};
}
```

On a later poll, a completed task means the next State was installed.
Setup failure never resolves the admitted input. A failed task still
owns private work and input needing a recorded outcome; caller shutdown/discard must
preserve that obligation.

```cpp
Result<bool> visit_epoch(loom::Worker& worker, EpochJob& job) {
  loom::poll(worker, {.messages = 32, .tasks = 8});
  if (job.task.state == loom::TaskState::failed)
    return std::unexpected(job.task.failure);
  if (job.task.state != loom::TaskState::done) return false;
  loom::detach(job.task);
  return true;
}
```

Execution can instead use another lane. Begin the Draft on the publication
lane, assigning its execution lane without touching that lane's resources.
Hand it off, perform Object work and freeze there. A synchronized handoff
establishes happens-before and keeps the sealed Draft, results and updates alive.
The publication thread consumes that result with the same preparation/publication calls:

```cpp
Progress install_frozen(journal::Consumer& consumer, const object::Draft& draft,
    std::span<const journal::EventResult> results, Ref<journal::State>& next,
    const journal::ObligationChanges* obligations = nullptr) {
  auto progress = journal::prepare(consumer, draft, results, next, obligations);
  if (progress.kind() != Progress::Kind::done) return progress;
  journal::publish(consumer, draft, std::move(next));
  return Progress::done();
}

} // namespace candidate
```

Releasing a lane does not transfer mutation authority. Cross-lane computation
needs explicitly assigned private work; this example supplies no automatic
parallel mutation or migration mechanism. Failure repair/discard never erases
admitted events. Discard closes private edits but keeps the same input and
previous State for repair/retry. Results and updates stay immutable and alive
across preparation waits until copied into the next State. Logical continuations
belong in those updates, not parked Tasks.
