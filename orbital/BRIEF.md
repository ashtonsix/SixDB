# SixDB Orbital Design Brief

SixDB succeeds Calico. Its Orbital module combines Calico xmem's consensus and MVCC with Calico Orbital's OS-like services, redesigned for an extensible HTAP+ETL database. A full OS/hypervisor remains a longer-term ambition. Orbital provides the runtime substrate for hot-swappable capabilities; Shore owns the extension ecosystem, registry, connectors and converters.

Orbital aims for near-optimal throughput, tail latency, network efficiency and disaster resilience. Under ideal conditions, local point writes should target below 500 µs p99.9, and data-loss-tolerant processing should reach sub-microsecond latency. These are targets, not measured results.

Techniques include WireGuard over IPv6 ULA, custom UDP transport, adaptive MTU/packet/FEC tuning, UFFD for access control and lazy/COW materialisation, io_uring for frequent network/filesystem operations, custom IPC, DMA and NUMA. Most syscalls use the ordinary path to keep sandboxing tractable.

## Scope and State

Orbital coordinates application state and work without interpreting their meaning. Engine owns database semantics, data structures and which changes must become visible together. Loom binds resources and schedules physical work. The same Orbital interface must support other applications' durable memory and actions.

Durable objects define their own scopes and conflict rules. A scope might cover a row, a byte range or a set of items. The owning module supplies complete descriptions of possible effects, read dependencies and how their scopes overlap; Orbital enforces these without interpreting the data. Following pointers or allocating internal storage can stay within an object's declared scope.

Each object may have several physical representations, provided they are losslessly convertible. Conversion may depend on other persisted artifacts, such as shared dictionaries.

## Execution

Producers supply events, witnesses admit them into ordered shard epochs, and consumers fold them into state:

`State[epoch] = Fold(State[epoch−1], Input[epoch])`

Consumers fold epochs in sequence and expose each fold's completed results in that order. Within an epoch, work may be reordered or run concurrently provided every permitted schedule reaches the same logical fixpoint, including ordering metadata and protocol outputs. When order matters, it is derived from agreed state and input. Plans may allow operations to combine if their complete observable results and transaction atomicity are preserved. Transactions can read preceding logical changes within the same epoch.

A transaction waiting for another shard or verification leaves a continuation. The epoch can finish and independent transactions can publish; later agreed input resumes the transaction. Ordering and resolution metadata must progress even while computation is blocked. Consumers may prepare later epochs speculatively.

A VM may serve several roles and shards. A control plane may publish membership, topology and health information.

## Persistence and Admission

A producer immediately starts a PLP write when it creates or receives an event. Registered data-loss-tolerant processors may run during the write, at most once per processor per event, and may have external effects. After its write completes, the producer sends the event to two followers outside its failure domain, typically an availability zone. Followers acknowledge after their own PLP writes. Two durable copies in distinct failure domains make the event eligible for admission.

Ordinarily, a shard has three witnesses: a leader and two followers, using a 2-of-3 quorum to agree a journal prefix. Leadership persists across epochs, so submissions need no fresh election or leadership-confirmation round trip.

An admission request names a producer stream and the highest LSN through which its contiguous prefix is persisted. Any witness can receive and forward the request. The leader merges these frontiers into epochs, admitting only new ranges and advancing each stream monotonically. Requests are idempotent. If routing information is stale, the witness can forward the request or the sender can resubmit it with the same stream and LSN. Receipt does not establish admission.

The leader durably accepts new journal entries and replicates them with that evidence. A follower that durably accepts the same entries may propagate without a return hop through the leader. Catching up a slow witness must not stall admission by the other two. Limit catch-up traffic separately so it cannot fill the buffers used for new admissions, while retaining the history needed for recovery. Nonvoting copies add no acknowledgement to ordinary admission.

## Transactions and Contention

Transactions have stable IDs and producer-assigned coordinators. A producer has one stream per shard for local transactions; cross-shard submissions impose no producer-stream ordering. Each shard chooses how to batch local execution (L), cross-shard preparation (C1) and installation (C2).

Before execution, the application declares its **effect envelope**: the scopes of every possible effect and the shards responsible for ordering them. Separate rules govern which output reservations conflict and which possible effects a read must account for. These rules must be deterministic and usable without waiting for execution.

A read must declare dependencies on everything that could invalidate its result, including effects outside the data it directly reads. Execution may discover new sources, but undeclared effects fail the transaction; the protocol does not expand the envelope and retry. Changes to scopes or their responsible shards must preserve outstanding plans.

A transaction reserves its output scopes to fix one serial position, then computes against it:

1. **Reserve output scopes.** Visit shards in a common order, acquiring each shard's required reservations all at once. Conflicting reservations are exclusive. Agreed queues prevent overtaking conflicting waiters; a transaction queues only at its current shard. Partial reservations do not block reads.
2. **Fix the position.** Once all reservations are held, each responsible shard announces a minimum position after prior committed or announced conflicting outputs and relevant read bounds. Affected reads at or above that minimum may wait. The coordinator chooses one unique position `c` satisfying every minimum, publishes it to all responsible shards and awaits acknowledgement. Fixing `c` narrows the interval of blocked reads. Release the reservations before computation or verification.
3. **Read and compute.** Before reading or waiting, register `c` on the read's dependency scopes. Later reservations for effects overlapping those dependencies get positions after `c`, without waiting for this transaction to finish. Read retained state at `c` plus the transaction's own tentative effects, waiting where necessary for earlier pending effects. Bounds covering all permitted reads can be registered once and reused. Newly discovered sources use the same `c`.
4. **Decide and install.** Store the complete outcome durably and finish all required synchronous checks before committing. Record one recoverable decision to commit or abort. Participants may install it at different times, but reads must preserve the application's declared atomic visibility. Resolve every announced output. A no-effect outcome creates no version and does not resolve earlier transactions' pending effects. Installation order does not change logical version order.

Positions share a total order without a global sequencer or physical clock. They respect known causality and requested freshness, but do not establish real-time order between independent clients. Ordering follows agreed history, not physical execution or message arrival. Read-only work chooses a retained snapshot without reserving outputs; applications may accept older inputs. A waiting transaction keeps its position and captured inputs.

Transactions retain no locks on source reads and need no arbitration components or contention retries. Acquiring reservations in a common order avoids cycles. Registering reads before waiting prevents new arrivals from continually adding predecessors. Progress requires fair service, finitely many predecessors, terminating programs and eventual resolution when an owner fails.

A broad read with narrow outputs lets independent source writers keep moving. Broad possible outputs can delay many reads even if execution changes little; describing them compactly does not reduce the interference. Dependencies and reservation queues can spread WAN delays to further transactions. Applications may need narrower envelopes, older inputs or smaller atomic units.

The application may declare that an effect completely replaces earlier effects over a scope. Once that replacement is committed and visible at the read's position, reads can skip the covered dependencies. Short local transactions can complete within one epoch under these same rules.

## Extensions and Publication

Extensions run as sandboxed WASM programs or processes inside Firecracker VMs. WASM uses a determinism-hardened runtime and is preferred. Unless specially approved, an extension uses one physical representation at its interface: identical input bytes produce identical output bytes. Approved extensions must still be logically deterministic; data and I/O permissions are separate.

With inputs available, WASM and approved native extensions can complete a chain within one epoch: extension → application request → extension → tentative effects. The application defines request meaning, encoding and effect coverage. Handlers join the parent transaction and must not wait for it to publish.

Other native extensions require BLAKE3 comparison of the full interaction and outcome, including requests, responses, effects and completion status. All executions required by the agreed plan must match; any consumer's digest can be the reference. A mismatch fails the transaction. Every required check must finish before commitment or publication, including read-only results. Independent transactions may publish meanwhile.

Initially, checked native execution is confined to one shard for all source and effect authorities. Establish its position, effect envelope and bounds covering all permitted reads before running. Queries then run privately without changing shared ordering metadata. Registering bounds does not wait for earlier computation; each query waits only for relevant predecessors, and independent source writers continue. Admit the checked outcome or failure through later agreed input. This avoids per-query checks at the cost of deferred publication, retained history and multiple executions.

Journal source and executable versions, runtime/configuration profiles and verification policy or approval. Retain these, reproducible inputs and accepted records for replay and audit. Persistent extension state and external facts are explicit inputs or transactional effects; process memory is disposable. Dispatch external writes from committed intents.

Deployment owners may approve native execution without synchronous checks and use asynchronous audits against the retained inputs and accepted record, comparing bytes or approved logical outcomes. Coverage, delay and retained evidence limit detection. A mismatch is an integrity incident requiring investigation and possible recovery; it neither identifies the correct result nor retroactively aborts committed work.

## Dissemination and Adaptation

Multiple independent origins may derive the same message. Pre-agreed arborescences connect origins to destinations; in-arborescences aggregate L stream frontiers toward witnesses. Followers start propagating after completing their PLP writes, with the producer or leader joining on the first acknowledgement. For the same agreed history, delivery order cannot change decisions or message contents. Duplicate delivery is harmless; propagating a tentative result does not make it authoritative.

Measured network behaviour guides flow, leader and witness choices. Retain good assignments and revalidate them rather than rotating on a schedule. Account for the path through a durable follower, its fallback and the cost of changing assignments. Changing a flow leaves authority unchanged. Changing a leader requires quorum recovery; changing witnesses transfers history and authority together. Detailed networking, routing and aggregation design remains open.

## Failure and Recovery

Compare recovery designs under the same workload, resource budget and incident timeline, including correlated failures, their duration and warning time:

| Goal | Measure of success |
| --- | --- |
| Stay available | Greatest distress sustained within throughput and tail-latency targets; interruption duration and affected workload. |
| Avoid data loss | Greatest destruction survived with zero durable work lost; warning needed to extend that protection. |
| Tolerate false alarms | Alarm frequency, concurrency and duration tolerated within service targets; response-induced slowdown and wasted resources. |
| Restore quickly | Newest complete pre-disaster prefix readable by each deadline; time to resume writes and restore redundancy. |

With witnesses in three independent domains, admission can continue after losing one domain, following leader recovery if needed. Two unavailable witnesses stop ordinary admission. Preserving durable work through one-domain loss also requires two independently placed copies of all recovery material. Consumers and storage need separate attention: even a healthy quorum does not ensure that data survives or can be served.

On an unusually slow response, use health probes to locate the problem and choose a proportional response. A brief, isolated anomaly should trigger bounded investigation and zero handoffs or admission pauses. If blob storage appears unhealthy, consumers should pull data and its dependencies into instance stores while it remains readable, retain local copies and share verified copies with peers. Keep unaffected work serving and limit recovery traffic so it cannot overwhelm healthy instances.

Growing evidence of danger can justify stronger protection for new work and copying exposed history to independent holders. Usable recovery capacity, verified checkpoints and journal tails outside threatened domains shorten both evacuation and later restoration. For relocation, copy while serving, then transfer the remaining suffix and an old-quorum certificate fixing the final prefix, closing old admission and naming the successor. Sudden regional destruction can preserve only the complete history already copied elsewhere.

Severe distress may justify an SOS: stop all local admission and immediately export useful state and evidence through surviving flows, without waiting for quorum or a complete snapshot. This supports both salvage and investigation when orderly relocation is no longer possible.

Restoration must recover a consistent prefix and its ordering constraints. Abandoning a suffix requires explicit point-in-time restore with old authority fenced; replay must not repeat external effects. Recovering a readable prefix, resuming writes and restoring redundancy are separate milestones. Their timings, and the tolerance for false alarms, remain unmeasured.
