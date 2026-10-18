# Using Loom in a complete operation

Start ready work with ordinary calls. Ikea's prepared bodies can run inline,
within a synchronous native chain or within a Loom callback. Compare those paths
using the same useful body. A Ring hides memory dependencies by running
independent work; a Task retains an accepted operation across yields or real
Signal waits. Several plan stages alone require neither boundary.

The [entry page](../README.md#choose-the-execution-path) maps these choices to
maintained examples. This guide covers their common operation boundaries; the
[Ring](ring.md), [execution policy](execution.md) and [hardware](hardware.md)
guides describe the specific interfaces.

## Prepare before the native region

| Prepare or admit once for the relevant lifetime | Reuse while that qualification holds |
| --- | --- |
| Adopt the assigned Worker thread; configure its retained Arena and Task capacity | Select the Pool at an ordinary allocation boundary; zero Task capacity permits native work |
| Admit actual Ikea windows, layout, activity and named plans/views | Call the selected unchecked/native bodies over those windows |
| Reserve output, stable operation frames and a typed Ring's active/overflow inventory | Push bounded work and spin the reused bank; a refused push consumes no work |
| Bind each abstract memory demand against its minimum contiguous qualified window | Carry the small DemandClass and prefetch only qualified active addresses |
| Qualify a complete direct/Ring pair and execution-policy options, if needed | Select once per operation; sample only when due and budgeted |
| Request an optional shared hardware profile and select coarse pressure | Copy policy at returned boundaries; native visits read no profile atomic or clock |

Prepared adapters borrow their named plans, views and backing bytes; the
[lifetime rules](#binding-and-lifetime) apply throughout their use. Expose
invariant layout and placement facts to the compiler in the useful body where
possible. A general bound view can still pay runtime stride/addressing costs
that an inlined contiguous placement avoids. Inspect the actual consumer while
preserving its admitted bounds and placement.

Reserve the resources a native step requires before entering it. Allocation or
preparation refusal belongs at admission or a returned boundary. The
[preparation example](../examples/preparation.cpp) handles stockout, bounded
pending/ready inventory and failed cleanup; placement of its Lane proves no NUMA
residence. The [partition example](../examples/partition.cpp) admits complete
output before native processing and keeps physical completion separate from
caller interest.

## Binding and lifetime

The assigned owner may borrow `bytes(Allocation)` during a nonsuspending owner
call while Allocation and bindings stay stable: no polling, reentry, growth,
release or owner change during the borrow. A complete synchronous Ring drain may
reuse those bindings across visits and spin returns within that call. Every
borrower must finish or be discarded before the call returns. The
[execution example](../examples/execution.cpp) uses this scope without acquiring
a Hold per callback.

Work escaping that scope acquires a Hold covering every byte it can use. A Hold
preserves the captured address and physical lifetime; content stability,
selected version and permission remain owner obligations. The
[hint example](../examples/hints.cpp) retains physical input through all admitted
jobs. Frame pointers, output pointers and common-prefix pointees need their own
equally stable lifetime through active work, overflow and cleanup.

`allocate(worker, request)` selects the Worker's Pool. `grow(allocation, request)`,
`hold(allocation, footprint)` and `retain(hold)` operate on the captured storage's
owner Lane and take no separate Worker argument. Growth uses `GrowthRequest`;
Hold acquisition/retention return checked Results, including saturation refusal.
Holds prevent relocation and permit in-place growth. Growth preserves readable
bytes without initializing new bytes. End overlapping readers before initializing
a new body or padding, then rebuild affected Ikea bindings. The
[allocation example](../examples/allocation.cpp) shows the complete foreign-read,
completion, growth and rebind sequence.

The consumer binds its container's storage and prepared access to the physical
service, retains its backing once for the operation and lends qualified work to
Loom. Loom neither instantiates the container's storage policy nor captures or
versions it on the caller's behalf.

Task detach ends its registration; it does not discard Ring frames. The owner
handles their cleanup separately at a returned boundary with `release_if` or
`discard`. Neither action undoes earlier stores or ends external physical uses.
Keep removed borrowers alive until cleanup returns. Close every Ring after drain
or explicit discard: even an empty configured bank retains its fixed Pool
allocation and prevents Pool shutdown.

## Find the useful dependency boundary

Keep cheap ready decoding, filtering, comparison and reduction together. A ready
kernel-to-kernel tail transfer can stay within one visit, without restoring
another Slot or rotating the Ring. Share canonical
1/2/4/8-byte lane consumers inside an already useful composed operation when that
removes substantial specialization. Keep direct packed algorithms and fuse cheap
neighbors such as two range comparisons after one decode. See
[ready composition](ring.md#compose-ready-work-before-rotating), Ikea's
[consumer guide](../../ikea/docs/consumers.md) for format-specific starting
patterns, and its [maintained filter examples](../../ikea/docs/filters.md).

For dependent memory work, load the current admitted metadata, derive the next
address within that binding, issue its hint and yield to
independent work. On the later visit, consume the dependency and repeat. The
[hint example](../examples/hints.cpp) demonstrates complete dependent chains.
The later load may still stall: a hint is not a completion event. Form payload
addresses only for active admitted items; separate planes or gaps require
separate qualified demands.

Read permission, predicted execution cost and producer completion are separate
facts. Hardware profiles and prefetches establish no producer completion. A real
I/O, capacity or producer wait uses the enclosing Task and its Signal protocol.
A small ready consumer can stay inline; the [execution policy](execution.md)
uses caller-cohort serial evidence to choose complete direct/Ring paths without
per-bucket probes.

## Batch useful work and bound service gaps

Ring capacity counts native frames; overflow bounds admission but does not add
active overlap until promoted. Different microtask kinds can share a rotation
when they use the same native signature and retain their own operation facts.
Count useful independent states; configured capacity need not be fully populated.
A dependency chain within one state is still serial.

Small [cohorts](ring.md#batching-and-service-budgets) can amortize dispatch across
several independent item stages. Preserve enough live independence while
increasing grain. More work per visit also delays each other frame and the next
owner service boundary. `spin({visits})` counts callbacks, not nanoseconds; a
four-state cohort can perform four item stages per callback. Bound each stage's
work and choose outer poll/time checks for the operation's service needs. The
illustrative visit/width values in examples are not universal presets.

When independent keys are already available, [bounded streams](stream.md) retain
the canonical prefetch/find loop and its lookahead across visits. They process a
fixed useful-key quantum without a separate job per lookup and can share a Ring
with other work. Choose the quantum for both throughput and service delay to the
other kinds.

Keep output/reduction batching inside the existing kernel where useful. A sparse
result mask requires neither a Ring admission per survivor nor sparse activity
in the next reader. That reader may have a much faster dense path. Reuse the
original dense activity only while its entire window remains admitted, then
intersect its result with the selected mask. Ikea's
[guarded example](../../ikea/examples/filters/guarded.cpp) demonstrates an ordinary
zero-result guard with that qualification.

## Tasks and physical completion

`start` accepts a fresh stable Task without running it inline. Yield and Signal
waits retain its accepted slot; notification queues a later visit. Completion,
failure and detach end registration once, leaving the result in the frame.
Each step returns Orbital's two-word `Progress`: done, yield, failure or a
borrowed Signal with its full observed generation. Use the factories/accessors;
the operation frame owns results, resumable state and retained resources.
Wait Signals must belong to the Worker's Lane. Detach at a returned owner
boundary and apply the [native cleanup rules](#binding-and-lifetime) separately.

Each Task step bounds its work. Signal notification synchronously queues all
enrolled waiters; poll budgets count Messages/Task visits, not that fanout or
elapsed time. A capacity wake grants no storage: recheck the condition. Physical
Message service is independent of Task capacity. Zero work returned by `poll`
does not prove idleness, including during incomplete Message publication.
The [capacity example](../examples/capacity.cpp) grants a finite equal-shape batch;
sustained arrivals and variable-size fitting remain caller policy.

For worker-to-worker handoffs, retain stable Message/ticket state through the
queue's final `release` callback. Returning credit from `handle` is too early for
immediate record reuse. A Message's physical queue use and a Task's registration
are distinct. The
[partition example](../examples/partition.cpp) returns ACK credit at final Message
release and settles reads/ACKs even after Task detach. It is a completion
pattern, not a general worker-group routing or cross-Pool migration policy.

Shutdown stops new Task admission; accepted operations may still allocate to
finish. Finish or detach them, settle native storage and keep polling physical
completion, handling cleanup failure. Copy final Pool statistics before shutdown
removes the Pool. Once shutdown is done, relinquish before destroying the Worker.
Stop external posting and retain backend records through final handling. A
drained queue does not prove all posting calls or their wake callbacks returned.

## Compare complete consumers

| Measurement | What it can establish |
| --- | --- |
| Dependent result-to-next-request elapsed time | Latency of that complete serial consumer, with normal CPU overlap |
| Time per useful request with independent work in flight | Reciprocal throughput at the stated concurrency; include admission, sampling and result work |
| Controlled cache/page/NUMA observations | Conditional behavior for that history and mapping, not present residence of arbitrary bytes |

Preserve useful work, format, result-dependent work and address trace when
comparing direct and Ring paths. Include fixed direct/Ring and observation-only
controls when attributing policy gains; charge probes and clocks without counting
repeated samples as additional useful requests. Compare warm repeated work as
well as changing memory dependencies. The [execution guide](execution.md) links
the preserved large-working-set Ring gains and losing cutoffs.

Ikea's [shared-stage measurements](../../workbench/spikes/filter-composition/RUNTIME.md)
support selective sharing inside existing CPS operations, not CPS entry for every
small filter. Its [graduated guard comparison](../../workbench/spikes/packed-filters/EARLY-GRADUATION.md)
shows cheaper dependent latency alongside worse throughput on mixed easy/hard
windows, and measurable cost from runtime placement facts. Both are finite warm
filter fixtures, not DRAM or Loom speedup measurements. Their masks and head
resolution rates do not classify cache residence.

Inspect the exact native caller with the pinned compiler: inlining, cold-path
ordinary calls, register/stack traffic, argument restoration and result-dependent
address selection matter at this scale. `musttail` enforces a transfer shape;
boundaries still cost work. Static instruction counts do not identify stalls,
IPC, servicing tiers or a hardware resource limit. Retain native timings and
independent useful-result checks for claims about a complete consumer.
