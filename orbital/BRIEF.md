# SixDB Orbital Design Brief

SixDB is a new database, the successor to Calico. Calico had among others two sub-modules: xmem (consensus and MVCC), and Orbital (OS-like substrate; disk, network, threads). SixDB's Orbital module will combine their functional areas, while redesigning from ground-up. Consurgent is the company that owns all of these technologies; the clearest concise account of the Calico prior art is in `~/consurgent/pitch/` (web slides and script).

Calico Orbital was conceived as a full OS+hypervisor, with Calico positioned as "one process among many". In my hubris, I underestimated the investment required to build a frontier-advancing hypervisor. That vision remains a long-term ambition, but Consurgent will need to stay focused on HTAP+ETL for longer than I initially anticipated. I now see SixDB as the central extensible locus: capability hot-swapping without a full hypervisor. Orbital provides the extensibility substrate while another module, Shore, owns the ecosystem and registry. Shore also owns SixDB-maintained extensions, including integration connectors and format converters.

Beyond extensibility, Orbital aims to enable near-optimal throughput, latency (including tail latency), network cost efficiency, and disaster resilience. Techniques utilised will include: Wireguard over IPv6 ULA, adaptive MTU/packet/FEC tuning, custom transport protocol over UDP, UFFD (access control, COW with zero-copy-optimisation, lazy materialisation), io_uring (high-frequency network and filesystem operations; most syscalls go through the ordinary path, to keep sandboxxing tractable), custom IPC, DMA and NUMA, arborescent dissemination, witness placement optimisation, and more.

SixDB's approach to consensus separates payload persistence from witness admission, admits stream frontiers instead of individual events, separates logical state from physical representation, exploits deterministic folding to fixpoints to reduce synchronisation overhead, and affords sub-microsecond latency to data-loss tolerant log processors.

## Execution and State

Orbital divides state into shards. Producers supply events, witnesses admit them into ordered epochs, and consumers fold those epochs into state:

`State[epoch] = Fold(State[epoch−1], Input[epoch])`

Consumers fold epochs in sequence. They may prepare later epochs speculatively, but expose results only in epoch order. Within an epoch, transactions may be reordered and progressed concurrently, provided every permitted schedule reaches the same logical fixpoint. This includes agreed execution metadata and protocol outputs. Orbital treats fixpoints as opaque; their semantics belong to the owning modules.

State consists of the durable objects returned by the fold. An object may have several physical representations if they are losslessly convertible. Conversion may depend on other persisted artifacts, such as shared dictionaries.

A VM may serve several roles and belong to several shards. A separate control plane may publish membership, topology and health information.

## Events and Persistence

A producer creates or receives an event and immediately begins a PLP write. Registered data-loss-tolerant processors run during the write and may have external effects. Each processor is invoked at most once per event.

After its write completes, the producer sends the event to two producer-followers outside its failure domain, typically an availability zone. Followers acknowledge after completing their own PLP writes.

An event is persisted once two VMs in distinct failure domains have durably stored it. Persisted events are eligible for admission.

## Consensus — Single Shard

During ordinary operation, a shard has three witnesses: an elected leader and two followers, using a 2-of-3 quorum.

Producers own log streams. An admission request identifies a stream and the highest LSN through which its contiguous prefix is persisted. Admission is idempotent.

The witness leader batches submissions into epochs containing only newly admitted stream ranges. Admitted LSNs advance monotonically within each stream. Those ranges constitute the epoch's input.

The leader writes the epoch and replicates it to its followers. Consumers receive the agreed epochs and fold them into state. Routing and propagation are described under Dissemination; leadership changes and recovery remain to be specified below.

## Consensus — Multiple Shards

Each producer owns one stream per shard admitting its shard-local transactions. Cross-shard submissions impose no producer-stream ordering.

A cross-shard transaction has an immutable ID and a producer-assigned coordinator, usually a participating shard. Its ID and coordinator remain fixed through retries, restarts and changes in participating shards.

Witnesses order three kinds of epoch: shard-local execution, L; cross-shard preparation, C1; and cross-shard execution, C2. Each shard chooses its own batching policy.

### Preparation and Execution

Cross-shard transactions prepare in C1 and execute in C2. C1 preparation discovers, shares, and protects C2 execution inputs.

Preparation is divided into parts. Each part runs within one shard and consumes inputs from the producer and preceding parts. Several parts may run in the same shard. The producer supplies the initial parts. As preparation proceeds parts may create successor parts.

For example, suppose transaction T computes $y := A_{j=B_i}$, where the producer identifies A and $B=f(A_i)$:

| Part  | Shard | Work                                                   |
| ----- | ----- | ------------------------------------------------------ |
| $p_1$ | A     | Lock and read $A_i$; compute B; identify $p_2$.        |
| $p_2$ | B     | Lock and read $B_i$; obtain index $j$; identify $p_3$. |
| $p_3$ | A     | Lock and read $A_j$; supply the value for $y$.         |

The producer supplies $p_1$; preparation discovers $p_2$ and then $p_3$.

Each C1 part acquires the locks needed to protect its reads, writes and predicates for serializability. Durable objects define lock scope and conflict semantics.

At each epoch boundary, a part holds either all its required locks or none. All consumers of a shard agree on a conflict-free set of granted locks. Parts of the same transaction may share protection.

When a part finishes its work, it shares its results with known dependents, and further parts discovered with the coordinator. Completed parts retain their results and locks while the remaining parts prepare; results are reshared as further dependencies are discovered.

The coordinator authorizes C2 when every part is complete. C2 executes the transaction and releases its locks.

### Contention and Yielding

L transactions ordinarily execute within a single epoch without retaining locks across epoch boundaries. L transactions and C1 parts retry when the epoch fold cannot satisfy their protection requirements. Retry scheduling derives solely from agreed state and epoch history. Individual retries are neither witness-journaled nor reported to the coordinator.

Retries may be batched into coordinated epochs, allowing compatible work to proceed and collecting unresolved contention for arbitration. An arbitration component is a connected component of transactions linked by incompatible protection requirements, including relevant existing lock holders. If T1 conflicts with T2 on one shard and T2 with T3 on another, all three belong to one component.

For contention that retries do not resolve, a component may reserve the lock scopes held or requested by its members. These provisional locks block new conflicting protection during arbitration; they may overlap existing locks because they reserve scopes without authorizing execution. Arbitration may outlast an epoch, so reservations must not be replaced so frequently that decisions become obsolete before they can take effect.

Arbitration uses lock and preparation information from the involved shards to choose which work can proceed and which parts should yield, with a shared arbitrator where needed. Decisions should weigh the work unblocked against the preparation discarded.

During preparation, a coordinator receiving a yield request may invalidate affected parts and their dependents, including derived C2 inputs; cancel a transaction if a participant appears unavailable; or reject the request.

## Dissemination

...

## Extensions

...

## Failure and Recovery

...
