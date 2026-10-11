# Loom

Loom connects database work to [Orbital](../orbital/README.md). Its embedded
runtime offers three paths: ordinary calls over ready bytes, typed
Rings for independent memory lookups, and Tasks for work that suspends.
Placement, the version-aware read cache and I/O orchestration remain in the
[joint design](../workbench/design/orbital/DESIGN.md).

[Engine](../engine/README.md) owns database meaning and legal work;
[Shore](../shore/README.md) owns external adapters; [Ikea](../ikea/README.md)
supplies formats and native bodies. A kernel stage need not become a Task.
The [execution study](../workbench/spikes/loom-execution/FINDINGS.md#minimum-viable-loom-recommendation)
informs these paths. Its prototypes and [Calico ancestry](../workbench/notebook/calico.md)
do not establish the runtime contracts.

## Allocation stage

An embedded [Worker](include/loom/worker.h) has a stable Lane for physical
Messages, a local allocation Pool and a bounded Task queue. Adopt the caller
thread, then configure the Worker with a retained Arena and a Task capacity.
Zero capacity still permits direct native work and physical Message service.

The assigned owner can borrow `bytes(Allocation)` during a nonsuspending visit
while it keeps Allocation and bindings stable: no polling, reentry, growth,
release or owner change during that borrow. Checked Ikea bindings qualify the
actual footprint.
Work that escapes this scope acquires a Hold, which keeps its captured address
and complete footprint alive. A Hold supplies no content stability, selected
version or access permission; the operation must preserve those separately.
`allocate(worker, request)` selects the Worker's Pool. `grow(allocation, request)`,
`hold(allocation, footprint)` and `retain(hold)` use Orbital's actual operations
on the captured storage's owner Lane; they take no separate Worker argument.
Growth takes a `GrowthRequest`; capture and retain return checked `Result<Hold>`.
Count saturation refuses a new Hold without changing the existing physical uses.
Holds prevent relocation and permit in-place growth. Growth preserves readable
bytes and does not initialize new bytes. The writer ends overlapping readers
before initializing its new body or padding, then rebuilds its Ikea bindings.

## Tasks and completion

`start` accepts a fresh stable Task without running it inline. Yield and Signal
waits preserve its accepted slot; notification queues a later visit. Completion,
failure and detach end the registration once, leaving the result in the frame.
Each step returns Orbital's two-word `Progress`: done, yield, failure, or a
borrowed Signal with its full observed generation. Construct it with the named
factories and inspect it through accessors. The operation frame keeps useful
results, resumable state and retained resources; `Progress` owns none of them.
Task steps must bound their own work. Polling Messages remains independent of
Task capacity, including allocator returns after caller interest ends.
The poll count measures handled Messages and visited Tasks; zero does not prove
safe idleness, including while a producer is publishing a Message.
Signal notification synchronously queues all enrolled waiters. Poll budgets
limit visits, not that fanout or elapsed time. A capacity wake grants no storage;
each Task rechecks its condition. The [capacity check](test/capacity.cpp) covers
one-credit contention, enrollment, detach and late foreign returns.

Shutdown stops new Task admission. Existing operations must finish or detach
and release their native storage; accepted Tasks can still allocate to finish.
Continue polling through physical completion and handle reported cleanup
failures. After `shutdown` returns done, relinquish the Lane before
destroying the Worker. Completion removes the closed Pool from the Arena;
copy any final Pool statistics before shutdown. An empty Task queue does not
establish physical drain.
Stop external posting and keep independently retained backend records alive
through final Message handling. A drained queue does not prove that all posting
calls, including their wake callbacks, have returned.

## Experimental Object callers

The [selected ObjectStore caller](../workbench/spikes/orbital-mvcc/callers/object_module.md)
uses an actual Worker to exercise coherent reads across suspension, publication,
catalog teardown and escaping Part completion. It reuses the selected Shape
material; interpretation and physical ownership follow the
[Object module's seams](../orbital/README.md#embedded-objects).
The [MVCC study](../workbench/spikes/orbital-mvcc/README.md) also retains the
[rejected-baseline callers](../workbench/spikes/orbital-mvcc/callers/README.md)
for native Rings and delayed private fills with current, cancelled and stale
completion. These caller compositions remain experimental scaffolding.

## Memory Rings

[Typed Rings](include/loom/ring.h) hold independent memory continuations in
actual Pool storage. A stable per-worker owner configures its concrete native
family once and reuses it across batches. Active capacity and overflow are
bounded; close requires drain or explicit discard. Even an empty configured
Ring retains storage and prevents Pool shutdown until it closes.
The bank's private fixed Allocation owns its bytes until owner-lane close;
the Ring never grows it and releases it only during close. Escaping input and
output uses retain their own Holds.
Its circular active frontier skips vacant capacity. Selective cleanup runs at a
returned boundary and preserves surviving rotation and overflow order.

Callbacks carry explicit scalar, pointer or vector arguments and tail-transfer
through `yield` with `[[clang::musttail]]`. Their pointers must name stable
qualified operation state, never a previous callback's local plan or View.
Native callbacks must not throw. The private tail signatures omit C++
`noexcept` because the pinned compiler rejects that combination with `musttail`.
Concrete argument families need pinned-compiler qualification. Ring spin budgets
count visits; outer code bounds callback grain and time between polls.

Callers describe memory through [MemoryDemand](include/loom/hints.h): irregular
or linear access and an approximate extent. `bind_demand` floors that request to
one of eight size classes within the minimum contiguous qualified window.
Keep the resulting class in a stable operation frame; `ring.prefetch(class,
address)` maps it through the Ring's hardware/pressure snapshot and emits a
bounded inline body. Disjoint planes need separate qualified demands; a bounding
envelope does not authorize hinting across a gap. Empty/inactive rows must not
form payload addresses. Hints confer no readiness, content stability or permission
to load the remainder of a cache line.

A [HardwareService](include/loom/hardware.h) is shared explicitly at startup.
It compares the complete reported CPU/environment context with the retained
table. A miss or partial hit returns known fields and defaults immediately and
queues bounded calibration of missing fields on one background thread. A fully
known hit needs no calibration. Newly qualified fields advance the profile
revision; retained table fields survive failed or contradictory probes.
Independent qualified fields may replace defaults;
censored, inconsistent, failed or cancelled observations cannot invent knowledge.
Background results stay local and conditional on foreground overlap. The caller
chooses the memory/time allowance and keeps the service until cancellation/join.
Cancellation and the time allowance are cooperative; scheduling and VM calls can
delay return beyond that allowance. The retained table is compiled into Loom;
[its generator](tools/generate_hardware_table.py) validates the original context
keys and records the input hash. Runtime lookup needs no Python or subprocess.
Workers without a service use reported geometry and provisional defaults.

Attach the startup handle in `configure`, then call `refresh_hints(worker)` and
`ring.refresh_hints()` at returned boundaries. `set_hint_pressure` accepts a
coarse resource-pressure observation; mapping heuristics remain provisional.
Pending frames retain their original qualified class envelopes, so policy may
change between spins. Geometry stays fixed until close/rebind. Native visits
query no hardware, clock, lock or profile atomic, and add no fields to their
callback signature. The width candidate guides caller admission; it does not
resize a live bank or count hardware miss resources. Fixed `Hint` controls remain
available for matched mechanism comparisons.

## Build and examples

Build `loom_validate` using [Building](../BUILDING.md) to run the Worker, Ring and
allocation caller checks. The compiled examples demonstrate complete
lifetimes:

- [Allocation](examples/allocation.cpp): grow a SeriesPack after a foreign reader
  finishes, then resume its waiting Task.
- [Partition work](examples/partition.cpp): reserve output under a shared Arena
  limit and settle physical reads and ACK credit after Task detach.
- [Capacity](examples/capacity.cpp): one coordinator grants a finite batch of
  equal-shape clients one allocation per visit. Stable records keep granted bytes
  after client detach; sustained arrivals and variable-size fitting remain policy.
- [Preparation](examples/preparation.cpp): construct finite and window storage on
  a placed Lane with zero Task capacity. Bound pending/ready inventory, handle
  stockout and failure, then settle work before stopping the service. Failed finite
  cleanup needs an explicit `retry_cleanup`; placement proves no NUMA residence.
- [Hints](examples/hints.cpp): serve held native input during a shared startup
  miss, copy policy at returned boundaries, and settle calibration and storage.

The [allocation consumer](../workbench/benchmarks/orbital/README.md) owns the composed
SeriesPack/TuplePack workloads and actual LMDB comparisons.

Optional `loom_ring_probe` and `loom_hold_probe` targets measure dispatch under
different active occupancy and actual Hold acquisition under shared or separate
Arenas. Their [sources](test/ring_probe.cpp) and [controls](test/hold_probe.cpp)
keep useful-work checks and native owner baselines. These mechanism timings do
not establish database throughput or request latency.
