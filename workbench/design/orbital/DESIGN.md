# Orbital and Loom: runtime and protocol direction

The [Orbital runtime](../../../orbital/README.md) owns allocation, preparation,
worker interfaces and the embedded Object catalog. [Loom](../../../loom/README.md) supplies direct
calls, Tasks and typed Rings. Journal, distributed authority/recovery, Port and
the read cache remain future protocol work.

**Orbital Stage 2 is being rebuilt with measured implementation changes.** Its first embedded Objects/Views
implementation was withdrawn into the [MVCC study](../../spikes/orbital-mvcc/README.md).
Performance was qualified too late to guide its architecture. The selected
catalog/history and physical Representation/Part/View ownership now live in
Orbital. Complete-operation comparisons guide changes to version metadata,
ownership, interpretation and call boundaries together. Further narrow LMDB
tuning is set aside in favour of wider workload discovery: the first measured
survey covers larger working sets and real representation conversion; append
and interleaved-read callers are qualified for the next measurements.
The [performance study](../../spikes/orbital-mvcc/performance/README.md) owns
results and remaining questions. Lower latency than LMDB with versioning remains
an objective; broad parity and headroom for Engine/Shore, worker communication
and I/O are unproven. Metadata, copies, capacity and settlement costs remain
visible alongside timings.

Engine and Ikea's composition of many blocks remain open for Ashton to design.
The study's consumer is disposable scaffolding: it exercises real SeriesPack
column Parts and TuplePack row Parts, including bidirectional 4-to-1 Part
conversion, without defining Engine navigation, membership, indexes, summaries
or container copy-on-write. Stage 3 adds multiple workers.

## Embedded implementation and comparisons

The selected `ObjectStore<Physical>` owns logical identity, completed local cuts,
coherent discovery, atomic publication and history retention. Its material
adapter connects that logical lifetime to Orbital's `Representation`, `Part`
and `View`, which own physical retention and escape. Interpretation, geometry
and record operations stay with Ikea and the caller. The study preserves the rejected
[baseline](../../spikes/orbital-mvcc/baseline/objects.md), its actual lifetime and
publication histories, and [matched LMDB evidence](../../spikes/orbital-mvcc/performance/README.md).
The [module guide](../../../orbital/README.md#embedded-objects) describes the
implemented lifetime and compilation seams; competing layouts and unapplied
controls remain experiments.
The [actual Loom caller](../../spikes/orbital-mvcc/callers/object_module.md)
exercises those seams through suspension, atomic publication, cancellation and
physical completion after the catalog and caller have ended.
The [staged Ring caller](../../spikes/orbital-mvcc/callers/object_ring.md) exercises
independent selected reads on one Worker. Neither caller establishes worker
scaling or disk/network performance. Local preparation accepts finished material
for short publication; holding that accepted packet across I/O would exclude
other preparation. Durable admission and asynchronous construction belong to
the outer protocol rather than the embedded publication step.
The earlier [composition experiments](../../spikes/orbital-stage2-composition/FINDINGS.md)
provide additional evidence and unresolved alternatives.

Any embedded candidate must preserve the relevant correctness distinctions:
coherent discovery versus resolved retention; logical publication versus physical
conversion; private construction versus accepted finished work; caller cancellation
versus actual physical completion. A version number alone supplies no authority.
Representation interpretation and dependencies must survive with retained bytes,
including when conversion changes the number of Parts. The wider consensus and
recovery obligations below remain separate from the single-owner experiment.

## Future protocol profile

The declarations under `orbital/candidate/` use `orbital::candidate` and compile
alongside runtime headers. They preserve the wider protocol questions below;
they are not a second supported embedded API. Candidate examples begin with
[a point read](examples/caller.md), [batch execution](examples/jobs.md),
[memory lookups](examples/native.md), [partitioned work](examples/partitions.md)
and [extensions and I/O](examples/extensions.md). Protocol calls have no runtime
definitions. Actual Allocation, Worker/Task and Ring mechanics remain module-owned.

In the candidate Object design, Objects belong to a shard. Representations belong
to a replica. A Part describes its bytes and format. A View gives code access to
those bytes. A **Draft** holds private edits; a published **State** makes a
finished batch visible.
Objects keep logical state; representations keep physical state. Only Draft
data can be edited; cache loading can still fill a Buffer.
State holds the object directory directly. Its nodes keep complete records and
their metadata alive, so newer States can share them after an older State is gone.
One Draft keeps its admitted input, complete previous State and private edits
through publication.

### Protocol seams

| Area | Responsibility |
| --- | --- |
| [Object](include/orbital/candidate/object.h) | Object → Representation → Part → View; private edits and cached bytes |
| [Allocation](../../../orbital/include/orbital/allocation.h) | Implemented Arena budget, Pools, allocation, retention and reuse |
| [Journal](include/orbital/candidate/journal.h) | Store input, admit batches, publish state, recover and coordinate transactions |
| [Port](include/orbital/candidate/port.h) | Read and write memory, files, blobs and networks |
| [Worker](../../../orbital/include/orbital/worker.h) | Implemented threads and message queues; future [isolated executors](include/orbital/candidate/executor.h) |
| [Loom](../../../loom/include/loom/worker.h) | Implemented Tasks and [typed Rings](../../../loom/include/loom/ring.h); future placement and cache policy |

Engine owns keys, schemas, indexes, query plans and legal partitions of work.
Ikea supplies physical formats, access requirements and native bodies for
reading, editing and reconstruction. The data owner chooses changes and maintains
row visibility, indexes and summaries. Shore owns connectors, external formats,
source progress and sinks. They call ordinary functions over Views; a kernel
stage need not become a Task.

In this design, published State, cached bytes and active access have different
lifetimes. A **Buffer** caches a Part's bytes, either in owned memory or by sharing an
immutable Source. Its generation changes when storage changes, not when another
State publishes. A View protects the addresses and access rights used by code.
Keeping memory allocated alone does not make its contents immutable.

The [mutable Packed consumer](../../benchmarks/orbital/README.md) exercises the
implemented division: Orbital owns storage, Ikea qualifies formats and native
transfers, the consumer owns lookup and row/index stability, and Loom controls
execution. Qualification happens around stable native work, with format metadata
and bytes valid throughout. Where their lifetimes coincide, construction and
lookup need no separate owner.

Row slots and allocator bindings supply neither Object identity nor versioning.
Packed's flat fences, mutable pages and immediate deletion cannot serve retained
immutable snapshots. Its 6304-byte pages use virtual windows; driver timings do
not establish finite allocator selection performance. This comparison layout
does not define Engine's index or State. The driver owns evidence and omitted costs.

## Execution and placement

Each Loom Worker runs queued **Tasks** and handles **Messages** on its assigned
thread. Its **Lane** stores the message queue and stays at the same address if
another thread takes over. The Worker owns its allocation Pool. The caller keeps
a stable BufferCache on that Lane in the future Object path; current operations
own reusable typed **Ring** banks from the Pool. Loom's cache and placement policy
remain provisional. Embedded use adopts the application thread; a ready point
call needs no worker round trip.

Only the assigned thread changes lane-local state. Other threads post Messages.
Placement never changes shard authority or replay semantics. Before transferring
a Worker to another thread, finish stack-borrowed accesses, close or retain read contexts,
and establish happens-before. Keep the Worker and its routes at stable addresses;
handle any thread-specific backend registrations separately.

Before shutdown, stop external users from creating more work for that Lane.
Finish or transfer their retained uses and messages. Drain caller-owned cached
backing, preparations and Views, and close typed banks before actual Worker
shutdown drains its Tasks and Pool. An empty queue or completed I/O does not prove
that nobody can still post a Message or return memory to the Lane.

### Choose the useful execution path

The [Loom findings](../../spikes/loom-execution/FINDINGS.md) support three paths:

| Work | Path |
| --- | --- |
| Ready bytes | Ordinary function over qualified bytes |
| Independent memory lookups | Optional local Ring |
| Work that must suspend | Task with an explicit place to resume |

The execution study favored direct SeriesPack directory/Bec256 work and Rings
for independent random chains in its fixtures. Streams needed larger chunks.
The maintained consumer's controls determine the choice for current workloads.

The operation keeps its typed Ring storage for reuse and supplies callbacks,
state and protected byte addresses. Give one operation exclusive use of a
bank for a bounded batch; drain or discard it before reusing it. Enqueue refusal
changes no progress or resources. If operations later share a Ring, cancellation
must remove only the relevant entries after `spin` returns. Different callback
types can use separate Rings without a common closure format.

The inner switch has a tiny memory-latency budget. Check access rights, retain lifetimes,
allocate, build hint policy and bind routes outside it. Callers qualify memory
demand once around stable bytes; native visits select a bounded hint body from
the Ring's policy snapshot. Adopt policy changes only at returned boundaries.
Prefetch does not
report that bytes are ready. Callback ABI, work per step, active width, hint volume,
overflow size and time between polls remain separate tuning choices. Accepted
entries need active Ring slots and enough execution time, not just spare capacity.
Ring yield rotates memory lookups; Task yield returns to the scheduler.

### Ordinary jobs and residency

`start` queues a fresh Task within the Worker's task limit; it never runs inline.
Refusal changes nothing. Waiting and yielded Tasks keep their accepted slot.
Completion, failure or detach returns it once. The caller keeps Task and operation
state alive until cleanup. Task acceptance is separate from Journal admission.
The returned `Progress` carries only control status or a borrowed Signal and
its complete generation observation. Keep operation results, selected-version
authority and retained physical work in the stable operation frame. They do not
become extra Task-return fields or native callback arguments merely to survive
a wait.

`poll` handles bounded Messages and runs bounded Task steps. Process completions
between chunks of native work. Each step must itself be bounded; a sampled clock
check gives only a soft limit. The return count measures work performed, not safe
idleness. Keep polling while physical work can complete; sleeping, stealing and
latency guarantees need their own design.

A Task waits on a live **Signal** assigned to its Lane. `complete` records a final
operation result; `pulse` reports a change in capacity, cached bytes or published
state. Read the generation before checking the condition, then register and
recheck. A changed generation retries; waking does not grant a resource. Shared
backends send notifications to the caller's Lane. Detach removes the wait, not
another thread's or backend's physical access.

In the proposed read cache, Loom picks Buffers to evict; Orbital checks safety.
Drafts keep separate Buffers, so edited bytes cannot answer a published read.
Eviction clears cached bytes and increments their storage generation. Pending
ViewRequests keep the indexed identity; otherwise eviction returns the cache
metadata slot too. Actual I/O retains its old target, and late completion cannot
fill replacement storage. Account for the move as well as the final placement:
old backing, replacement bytes and transfer scratch can overlap. Shared aliases
do not duplicate an Arena charge; retained Sources/Requests delay its release.
Cancellation is not a refund. Use actual allocator/retirement state, not cache
membership alone, when estimating available capacity. Idle reusable allocations
remain charged until the allocator returns budget.

Reserve output and completion capacity before taking input access that cannot
be released. For unknown output size, choose a valid streaming, reconstruction
or spill point. Limiting Task count cannot break every resource cycle.
The [mixed path](examples/native.md#move-one-item-to-a-slow-path) changes the same
Task's continuation; it preserves finished work and the selected data version.

### Work across workers

Bind routes and CPU/cache placement before a batch. A set of Workers is not an
allocation Pool or shard. Direct channels can coexist with coordinators, but
forwarding every packet added hops and contention in the measured fixture.
Aggregation may justify coordination; that policy remains open.

Prefer choosing a worker for new legal work before Task acceptance. Here, a Task
stays with its Worker: it can change continuation or send retained partition work,
and the same Worker can hand off to another thread. These paths preserve the Task's
slot, selected version and physical obligations. Task migration between Workers
remains open. A placement proposal cannot move a live Ring or View, or retarget
an accepted Request.

A work channel may refuse new work. Reserved completion Messages and memory
returns must still run when it is full. Reserve receiving/output credit before
producing data and keep it through physical completion and required ACK or
reconciliation. Do not put an ACK behind work waiting for the credit it returns.
Cancellation must discard unwanted output and return credit even after the
Task disappears, including when completion arrived before cancellation.
[Partition work](examples/partitions.md) shows that ownership.

Choose local execution, shared reads or copying from actual reuse, skew, transfer
cost, output cost and total CPU work. Faster copied probes do not automatically
repay copying. A policy result is a proposal: before acting, recheck current
capacity, backing generations and applicable authority at the actual owners.
Feasible totals do not prove that a sequence of moves can make progress; resource
dependencies and reserved completion/return service still matter. Reserve actual
capacity for each started move; a final assignment does not authorize starting
every move concurrently.

Balance and desired headroom are policy targets; how to repair them after failure
remains open. A solver's treatment of an already-broken target cannot relax
allocator limits, access/version checks or Journal guarantees.
Keep sampled observations, move costs and reasons for refusal with coarse
placement decisions, outside ordinary ready calls and Ring switches.
Evidence covers same-host LLC domains in single-NUMA guests. Cross-NUMA/host
routing, observation cadence, search policy and concrete planning interfaces
remain open, along with cache ranking, adaptive controls and stealing.

## Future admitted-input path

This Object/Journal path is proposed beyond the embedded finished-result profile.
Input can be stored in memory and batches admitted locally. This protects it
while the instance lives. Crash durability requires stronger storage.

**Read.** Enroll one reusable reader record. Its ReadContext protects the selected
State without a shared State retain for each point call. Select Object,
Representation and Part, open a View and run code. A query can reuse context and
View. Close or retain the context before another acquisition, independent use or
transfer. The ready path needs no Task, ViewRequest, range copy or allocation.

Check bounds, rights, initialized bytes, alignment and padding when opening the
View. Buffer owns the Allocation or protected immutable Source; proposed View
registration keeps backing stable without another capture per call. Footprints,
authority and decoding metadata must remain valid through use. An open View stays
at its address and closes on its assigned thread. The registration mechanism and
its cost remain unimplemented. The allocator's Hold protects physical lifetime
and prevents relocation; it does not track a content-lock high-water mark.
Growth preserves existing readable bytes and leaves new body/padding
initialization to the writer. Ikea and the caller must authorize writes against
actual overlapping physical footprints, including padding and preserving stores.

**Write.** Store input → admit batch → edit Draft → publish State:

1. Engine/Shore supplies an immutable Event with input, replay code and dependencies.
   MemoryLog owns events and retry/retirement indexes. Reserve capacity first;
   direct `append_ready` validates the Producer, Log and Lane without provider
   dispatch. Matching retries preserve identity; retired IDs stay retired.
   Stronger Log backends store input and checkpoints with stronger guarantees.
2. Journal forms an ordered Batch in reserved storage and admits it. Local ordering
   needs no witness trip; witnessed ordering needs its configured proof.
   AdmittedBatch records that admission; formed input alone is insufficient.
3. Journal begins one Draft, retaining admitted input and the complete previous
   State. It assigns the execution lane without acquiring its resources; hand
   off before editing on another lane. Engine/Ikea performs edits and all
   membership, format, index and summary maintenance. Later events
   in this batch see prior edits; published readers still see the previous State.
   Native operations can run on ordinary threads under stable Views and authority.
   Partition physical stores safely—different logical fields can share a machine
   word—and join accesses before capture or freeze.
4. Finish writers and retain immutable Material: bytes, format metadata, decoding
   code and unchanged dependencies. Close mutable aliases before sharing memory;
   otherwise copy. Changed body bytes alone are insufficient: Bec256 empty/full
   encodings have different values despite both having zero-byte bodies.
5. Freeze the Draft, closing write authority. Journal checks its input and previous
   State, then prepares the next State, including results and obligations. This
   can wait. After final checks, publish without suspension, allocation, new I/O
   or failure. Objects, epoch, results and obligations become visible together.
6. Derived writes can run later when stored input supplies a valid recovery path.
   Bound queued writes, retained history and reconstruction work; count their
   maintenance. States can share unchanged directory nodes, including an empty
   object directory as a valid result.

Storing input, admitting a batch and publishing state are different facts.
MemoryLog's Log keeps its provider alive; retiring old input preserves identity
boundaries without keeping every result forever. Unsupported synchronous providers
return `unavailable` before effects; other failures need reconciliation.
Batching must not force an idle point operation to wait for a full batch.
Progress advances only after completed work. Partial edits or imports need repair,
discard or a valid recorded outcome. Discard closes private edits but preserves
the same input and previous State for retry. Dropping a Task or Draft never
resolves admitted input.

## Missing bytes, memory and escape

A **ViewRequest** captures the chosen Material, version, access requirements and
lock once. The caller keeps borrowed ranges alive through loading and View use.
It can release ReadContext after copying any decoding metadata needed later.
The load function supplies missing ranges from Sources that identify their
storage backends.

The request keeps data identity, not resident memory. Actual transfers hold their
target addresses and storage generation, including before submission when the
backend has no capacity. Retry after relocation uses the same Material/version;
late completion cannot populate a replacement. Expose a View only after proving
its coverage and registering its access. Cancellation never frees bytes still
used by I/O.

When access outlives its native scope, create a **RetainedView** in independently
owned storage. It copies ranges and keeps Buffer, Lock and active byte protection
alive. It adds no rights. Isolated extensions check exact ranges, initialized
bytes, padding and authority again. Release the retained View on its assigned
Lane after final use, independently of the former Task. Clear borrowed Draft
pointers before closing locks or destroying the Draft.

**Arena** owns memory records and the shared byte budget. Each allocation **Pool**
keeps reusable storage on one Lane. Allocation owns storage; Hold keeps addresses
and padding valid without freezing contents. Compatible reuse needs no Arena
queue trip. Growth preserves protected addresses and padding; failure preserves
old bytes. The [implemented allocator](../../../orbital/README.md) coalesces foreign
returns into one reserved Message per packing group or virtual window. Return
shared budget only after final use and Message handling. Capacity notifications
report a change, not a grant. Legal shared-budget refusal enrolls its Pool for
one future refund notice; uninterested Pools receive no broadcast. The owner
observes the generation before attempting allocation and rechecks after waking.
HOT/WARM/COLD hints guide reclamation without proving cache residence. Reuse
prefers physically prepared storage without ranking temperature or CPU advice.
Reclaim cold memory asynchronously; VM changes can still disturb workers through TLB activity.
Selection caches shorten searches; the storage owner still checks geometry and
physical availability. A cache hit grants neither bytes nor permission to reuse them.
A Part need not have its own file descriptor or mapping.

For predictable demand, prepare storage and its format before a caller needs it.
Orbital reserves budget and keeps the original Pool as owner. Loom chooses a
preparation Lane. Orbital prefaults the accepted extent and initializes readable
padding; Ikea or the data owner constructs the body and private format metadata
there. The Pool's assigned thread publishes the Allocation; the caller adopts
the format metadata and local accounting. Ready use stays an ordinary call; exhausted
inventory can fall back to foreground work or wait under caller policy.
Count ready and in-flight inventory together. Cancellation ends demand, not
physical work or its charge. Cleanup refunds only confirmed discarded pages.
If cleanup fails, retain the physical record and its charge for an explicit retry;
Pool close reports failure while that debt remains. Unrelated ready work can continue.

Preparation can occupy its Lane for the whole VM/constructor call. Choose a
service Lane whose other work can tolerate that; count-limited polling does not
bound the duration. Posting queues work; a sleeping service also needs notification.
Stop new submissions and settle physical work before stopping
that service. The [preparation interface](../../../orbital/include/orbital/preparation.h)
owns this physical protocol; ready inventory and fallback policy belong to its caller.

The proposed Port backends produce immutable Sources and writable Destinations.
A Request fixes its endpoint and ranges until cleanup. The backend calls `finish`
after physical accesses end. The caller handles its completion Message and then
completes the Signal; on that same thread it can do this directly. Keep storage
through both access and Message handling. None of these steps publishes a State.

## Provisional full profile

**Ordering and storage.** Store complete input and replay code in independent
failure domains, then learn witness agreement. Three witnesses and a 2-of-3
ordinary path remain a proposal. Journal enforces voting rules and encodes records;
Log stores them and Port transports them. Stored bytes alone do not prove
admission or a valid vote. Witness replies keep accepted-history storage alive.
Recover the compatible accepted prefix before extending it. Storage evidence
names actual incarnations and protection generations; ordering authority does
not prove bytes are available. Producer sequence, epoch and transaction position
remain distinct.

**Transactions.** Reserve participants in common order, durably fix their serial
positions, release reservations, then execute after all fixes. Engine declares
outputs and reads, including absence/range changes. Preserve caller causality and
resolve every output. Historical reads return the requested State or agreed
unavailability, never a substitute.

State can keep unresolved transactions, source fallbacks, logical continuations,
outbox, source progress and audits. These also support embedded ELT. Omitting
updates preserves existing obligations. Independent work can finish while a
transaction waits. Later agreed input resumes its recorded continuation; local
pressure or timeout cannot choose another result.

**Extensions and effects.** WASM is the preferred isolated-extension direction;
Firecracker is another executor. Enforce exact ranges and rights, including
partial pages. Recorded code/profile fixes runtime semantics. The owner chooses
checking/audit policy; the executor compares the complete interaction and main result.
Fresh observations need agreed input before import. Engine/Ikea checks original
authority and imports with all maintenance. The executor never publishes; a later
audit incident cannot retrospectively abort an installed result.

Published outbox intent supplies stable contribution IDs. Non-deduplicating
sinks can leave unknown effects needing reconciliation. Early loss-tolerant
processing remains a separate explicit path. Preserve identity and receiver credit.

**Recovery.** Adopt checkpoints only with code, data, state and unresolved work
stored; preserve accepted input and old readers. Restore lazily; missing bytes
cannot serve. Recovery, replay, delivery and active access all constrain cleanup.
Late completion never replaces newer published state. Copying bytes transfers
no authority. Handoff certifies the closed prefix and transfers authority; PITR
fences the old deployment. Bootstrap/salvage must work when normal objects cannot
serve. Readability, resumed writes and restored redundancy remain different
outcomes. File layouts and wire formats remain open.

## Build and review

```sh
orb -m ubuntu cmake -S . -B build/design/orbital/clang -G Ninja
orb -m ubuntu cmake --build build/design/orbital/clang --target orbital_design_check
```

Eleven declaration/example TUs compile independently, outside the default build, against the
actual Orbital/Loom libraries. Python extracts the
pages' cpp blocks in reading order; diagnostics point back to Markdown.
This skeleton checks future Object, Journal, Port and executor call shapes.
Compilation proves neither correctness nor speed. Module headers own implemented
allocation, reclamation, scheduling and the embedded Object catalog. The MVCC
study owns the disposable caller, competing layouts and measured comparisons;
wire layouts remain open.

The embedded experiment leaves the wider seams open: stored input and
replay authority, missing-byte reconstruction/Port, distributed cuts and recovery,
and multiple-worker execution/reclamation. Engine owns its future navigation,
membership and multi-block composition. The temporary consumer does not settle
those choices.

LMDB's page-local navigation, inline records, positioned scans and local split
repair are leads for that composition. They do not justify a fixture-specific
optimization layer. The [completed allocation comparison](../../benchmarks/orbital/evidence/selected-allocation-20261007/README.md)
retains large point-read and insertion-tail gaps; same-CPU preparation did not
repair the tails. It measures the earlier `595e07fa` cut, with snapshot/publication
costs omitted. That headroom cannot price the Object path. The
[versioned comparison](../../spikes/orbital-mvcc/performance/README.md)
retains the rejected baseline's selection and publication with matched reader groups,
identical encoded Tuple payloads and the same legal Ikea native body. Its local
ARM results retain small scalar-read deficits; the fixed updates include unequal
adapter preparation and cannot isolate publication cost. It does not price
insertion/split tails, durability or multiple workers. Shared orchestration stays
compiled once; native specialization remains local and evidence-led.

## Evidence behind the recommendation

[Views](../../spikes/orbital-views/FINDINGS.md),
[objects](../../spikes/orbital-objects/FINDINGS.md),
[view control](../../spikes/orbital-views/control/README.md) and
[compositions](../../spikes/orbital-views/systems/README.md) inform access protection
and the separation of stored input, publication and physical completion.
Their prototypes are evidence, not contracts for these names or layouts.

[Memory descriptors](../../spikes/memory-descriptors/README.md),
[residency](../../spikes/orbital-views/residency/README.md),
[handoff](../../spikes/orbital-local-handoff/FINDINGS.md) and
[TLB work](../../spikes/orbital-views/tlb/README.md) inform memory placement.

Rebalancer's [transition accounting](https://facebook.github.io/rebalancer/docs/reference/common/utilization-definitions/)
and [constraint policies](https://facebook.github.io/rebalancer/docs/reference/constraint-policy/)
inform the placement distinctions above; they do not establish a solver choice.

[Networking](../../spikes/cft-commit-latency/RESULTS.md),
[formal assessment](../../../orbital/spec/RESULTS.md) and
[simulator models](../../simulator/models/README.md) retain narrower evidence.
[Ikea integration](../../../ikea/docs/integration.md),
[publication](../../spikes/orbital-views/prior-publication.md) and the
[handover](../../notebook/orbital/reference/handover-2026-10-03.md) separate owner
semantics, provisional mechanisms and user choices.
