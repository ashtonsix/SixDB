# SixDB Orbital Design Brief

SixDB is a new database, the successor to Calico. Calico had among others two sub-modules: xmem (consensus and MVCC), and Orbital (OS-like substrate; disk, network, threads). SixDB's Orbital module will combine their functional areas, while redesigning from ground-up. Consurgent is the company that owns all of these technologies; the clearest concise account of the Calico prior art is in `~/consurgent/pitch/` (web slides and script).

Calico Orbital was conceived as a full OS+hypervisor, with Calico positioned as "one process among many". In my hubris, I underestimated the investment required to build a frontier-advancing hypervisor. That vision remains a long-term ambition, but Consurgent will need to stay focused on HTAP+ETL for longer than I initially anticipated. I now see SixDB as the central extensible locus: capability hot-swapping without a full hypervisor. Orbital provides the extensibility substrate while another module, Shore, owns the ecosystem and registry. Shore also owns SixDB-maintained extensions, including integration connectors and format converters.

Beyond extensibility, Orbital aims to enable near-optimal throughput, latency (including tail latency), network cost efficiency, and disaster resilience. Techniques utilised will include: Wireguard over IPv6 ULA, adaptive MTU/packet/FEC tuning, custom transport protocol over UDP, UFFD (access control, COW with zero-copy-optimisation, lazy materialisation), io_uring (high-frequency network and filesystem operations; most syscalls go through the ordinary path, to keep sandboxxing tractable), custom IPC, DMA and NUMA, page-remapping for container growth, arborescent broadcasting, AZ placement optimisation, and more.

SixDB's approach to consensus separates payload persistence from witness admission, admits stream frontiers instead of individual events, separates logical state from physical representation, exploits deterministic folding to fixpoints to reduce synchronisation overhead, and affords sub-microsecond latency to data-loss tolerant log processors.

## Consensus — Single Shard

Within a shard, a VM may be a witness, failover-witness, producer, producer-follower, consumer and/or relay. Producers own log streams, witnesses admit stream ranges into epochs, and consumers deterministically fold epochs to produce state. For each shard, `State[epoch] = Fold(State[epoch−1], Input[epoch])`. During ordinary operations, a shard hosts exactly three witnesses in a 2-of-3 quorum, among which is an elected leader and two followers.

The steps an event goes through:

1. **Collection.** A producer produces or receives an event from an external source. It immediately begins a PLP write. While writing, registered data-loss-tolerant processors run; these may have external effects. Each processor is invoked at most once per event.

2. **Persistence.** Once the producer’s write completes, it forwards the event to two producer-followers outside the producer’s failure domain, typically an AZ. Producer-followers ack upon completing their PLP writes, and begin propagating events along their branch of a producer-selected arborescence. The producer joins propagation upon receiving its first acknowledgment.

   An event is persisted once durably stored by two VMs in distinct failure domains.

3. **Admission.** Among the producer and its two followers the one with the most direct route to the witness leader initiates idempotent admission, beginning once it knows the stream prefix is persisted through that LSN. Unless it is itself the leader, it submits the stream ID and LSN through an in-arborescence. Intermediate relays deduplicate by stream, taking the maximum LSN, and batch submissions together.

   The witness leader accepts each submission batch as an epoch, modifying it only to remove already-admitted ranges and ensure stream LSNs increase monotonically. Each epoch’s input consists of the newly admitted ranges.

   After completing its PLP write, the leader shares the epoch with its followers. Followers begin propagating epochs to consumers through a leader-selected arborescence after completing their writes; the leader joins upon receiving its first acknowledgment.

4. **Folding.** Consumers fold epochs in sequence. They may speculatively prepare later epochs, but never expose epoch results out of order. Each epoch holds a collection of transactions, which may be reordered and progressed concurrently provided all permissible schedules converge to the same logical fixpoint.

Fold-returned durable objects constitute the state. Each durable object may permit multiple physical representations, provided all are losslessly convertible to one another. Conversion may depend on persisted artifacts outside the object itself, such as shared dictionaries.

Orbital treats fixpoints as opaque; their semantics belong to the owning modules.

## Consensus — Multiple Shards

Deployments have many shards; VMs may hold several shard memberships. A separate control plane may publish membership, topology and health information.

Producers own one log stream per shard receiving their frontier-aggregated shard-local transactions. Cross-shard submissions impose no producer-stream ordering. Each cross-shard transaction has an immutable ID and a producer-assigned coordinator, typically a participating shard, whose identity remains fixed through retries and participant changes.

A cross-shard transaction comprises preparation in C1 and application in C2. Preparation consists of identifiable C1 parts, each executing within one shard. Several parts may execute within the same shard. The producer identifies the initial parts; parts may identify further parts and dependencies, sharing these with the coordinator and affected shards.

After persistence, the producer and its followers propagate the event to the coordinating witness leader and witness leaders hosting initial parts for idempotent admission. Relays batch cross-shard and shard-local events separately; shards need not agree on batch policy. Witness leaders interleave C1 and C2 epochs with shard-local L epochs, canonicalising order before propagation to consumers.

Multi-shard deployments require write locks. Durable objects own lock scope and conflict semantics, protecting the reads, writes and predicates required for serializability. Each C1 part acquires its required locks and consumes transaction inputs or results of preceding parts. Results propagate through leader-selected shard-to-shard arborescences. At epoch boundaries, each part holds its complete required lock set or none; retained lock state is conflict-free and identical among consumers of that shard. Completed parts retain their protection while later parts acquire theirs; parts of the same transaction may share protection.

L transactions and C1 parts conflicting with locks held by other transactions fail immediately. Same-epoch contenders are arbitrated by descending local retry count, then by a deterministic, module-owned policy opaque to Orbital; losers fail. Failed L transactions and parts retry locally. Retries are not witness-journaled: their placement, counts and ordering derive solely from agreed state and epoch history. Ordinary failures are not reported to the coordinator. Changed locks or matching data may cause further failure.

A part’s completion reports its result, dependencies and every further part it identified. The coordinator accounts for newly identified work before counting the reporting part as complete. Dependencies include both consumed results and decisions determining whether and where a part exists. A completed part retains its result and required protection until C2, cancellation or coordinator-authorized invalidation.

Preparation is complete when all required parts are complete, all discoveries are accounted for, and their results and protection remain valid. The coordinator then authorizes C2, identifying the parts and results to apply, and broadcasts the decision for admission at participating shards. C2 applies the transaction and releases its locks.

To release retained C1 locks before reaching C2, a shard sends the coordinator a resignation notice identifying the parts concerned. The coordinator may authorize C2 if preparation is complete, cancel the transaction and stop its retries, or invalidate selected parts before authorizing lock release and rescheduling. Rescheduling must provide an opportunity for blocked work to proceed.

Invalidating a part withdraws its result and all completed or pending work whose inputs or existence depend directly or indirectly on it. Independent branches may remain valid. Re-execution discovers dependent work afresh; previous destinations and arguments cannot be assumed unchanged. A lock supporting retained parts cannot be released merely because another part using it was invalidated. Breaking a lock cycle therefore requires invalidating enough holding work to release a blocking lock; retrying only blocked parts is insufficient.

The coordinator orders decisions, part registration and invalidations in its agreed history; affected shards apply their effects through agreed epoch histories. Decided C2 cannot be invalidated, and invalidated work cannot support C2. Messages identify the relevant transaction and parts and distinguish current from invalidated results; duplicates and stale discoveries cannot revive withdrawn work. A local retry does not create a separately identified protocol entity.

Object-owned reservations reduce retries. Scoped like locks, they record a threshold N and disallow overlapping new C1 locks from parts with retry counts below N. Failed L transactions or C1 parts may create reservations at their retry counts; repeated failure may expand their scope to the entire database state. Creation, replacement and release policies must be deterministic.

### Worked example

For transaction T, let \(y := A\_{B_i}\), where A is event-identified and \(B=f(A_i)\).

| Part    | Shard | Work                                                         |
| ------- | ----- | ------------------------------------------------------------ |
| \(p_1\) | A     | Lock and read \(A_i\); compute B; identify \(p_2\).          |
| \(p_2\) | B     | Lock and read \(B_i\); obtain index \(j\); identify \(p_3\). |
| \(p_3\) | A     | Lock and read \(A_j\); supply the value for \(y\).           |

The producer identifies \(p_1\). Its completion accounts for \(p_2\), whose completion accounts for \(p_3\). Although \(p_3\) introduces no new shard, it introduces outstanding work, so T cannot enter C2 before it completes.

Suppose another transaction U has completed a part holding \(A_j\), and a later U part requires \(A_i\). T’s \(p_3\) fails because U holds \(A_j\); U’s later part fails because T holds \(A_i\). Repeated local retries cannot resolve the cycle.

Shard A requests resignation of U’s part holding \(A_j\). U’s coordinator authorizes invalidation of that part and its dependent work. Their results become ineligible for C2, and A releases \(A_j\), assuming no retained U part still requires its protection.

U’s rescheduling leaves a window for T’s \(p_3\) to acquire \(A_j\). T completes preparation; its coordinator authorizes C2, which writes \(y\) and releases T’s locks. U can then resume, rediscovering its dependent work from fresh results rather than reusing the withdrawn chain.

## Consensus — Failure and Recovery

...

## Consensus — Extension

...

## Consensus — Optimisation

...

<!-- Analysis in relay -->

1.

R

This is deterministic and does not require witness consultation. 2. Lock placed during earlier G1 epoch.

<!-- For rapid disaster response (failover and/or quorum scaling), some shard members may hold dormant witness roles. -->

for each journal they submit events to. Witnesses admit log ranges into epochs. SixDB's logical state is produced with a deterministic fold over admitted epochs, applied by each consumers independently. Epochs are folded in order. consumers may prepare later epochs speculatively, but only expose results after epoch completion.

Event submission proceeds like so:

1. Collection. A producer produces or recieves an event from an external source. It immediately begins a PLP write, and runs registered data-loss tolerant processors; these processors may have external effects.
2. Persistence. The event is sent to a VM outside the producer's failure domain. The event is considered to be durable as soon as two VMs in distinct failure domains (typically AZs) have completed PLP writes. As soon as a recipient completes its write it acks and broadcasts to the entire shard.
   - Broadcasts are arborescent.

To non-consumers witnesses, producers only need send their ID and current max LSN.

Messages are sent arborescently

; dormant reserve witnesses can be quickly recruited in case of infrastructure distress. Among the witnesses is an elected leader.

Weighted blend of cost, latency and noise; family different weights and noise masks. Neighbour of node.

Events proceed through four stages:

1. Collection
2. Persistence
3. Admission
4. Application

Propagation, Submission

## Consensus Algorithm — Failure and Recovery

## Consensus Algorithm — Extensions

re-attempt might change discovery relinquishment

R, G1, G2

---

I wrote the broader Orbital brief, and generated the more-focused consensus spec. I expect these have drifted apart and need reconcilliation somehow. I'm thinking it'd be best to just have one brief, but structure it better rather than meander (could you propose an outline; for the consensus part, I'm thinking we should cover single-shard comprehenisvely first, and then introduce multiple shards). I'm thinking rather than brief->spec->TLA->reference->implementation we should do detailed brief -> simulated model. the simulated model would include VMs, shards, transactions, network, etc (not real infra, just simulated), and have a toy application based on the exchange of points (points instead of money permits more contrived transaction types, such as a write that targets every row in a shard, for probing system behaviour); the model can predict how long transactions will actually take to commit/propagate/etc (disk/network/everything modelled), supports fault injection, analysis for bottlenecks, algorithms can be swapped in and out, and it's a general workbench for both identifying corretness and performance issues and iterating before we do the TLA model and implementation (the workbench is also the reference).

There are also two areas I want more substantial changes:

1. Automatic recovery after quorum loss
2. Global transactions

On (1), it's a fool's errand to attempt automatic recovery with a possibility of data loss (>2 witnesses lost simultaenously, including leader); this should demand operator intervention, and go through the disaster recovery path rather than ordinary operating procedure. Rather than automating what happens after a disaster, focus should go towards what happens before a disaster; this means responding to health signals/probes, and shortening windows where the quorum is vulnerable. On a historically anomalous ack time (eg, 100ms for cross-AZ), we might run health probes immediately, and then calibrate rapidity and scale of response to those probes; if an entire region seems unhealthy, we might recruit new witnesses from outside the failure zone, change the consensus predicate to reduce data loss risk, possibly pause accepting commits, or maybe even do some kind of emergency SOS; or on the other hand, maybe we just wait for what appears to be a transient issue to pass; these health probes can be either reactive as-described, or background signals; altogether, they paint a picture of how we should respond. If we do include the path where a data shard quorum forms to elect new witnesses automatically, zero data loss should be garunteed; maybe the "emergency SOS" says something to the effect of "we believe the servers in GCP-europe are healthy, and give the instances there permission to elect new witnesses. we won't admit anything more for now.", and if your a data shard member who gets that SOS you know the data is safe, that authority has been safely revoked, and who can elect new witnesses.

On (2), we need a redo. There just hasn't been enough attention to conflict and the "continuations" were poorly thought through. Let all regional transactions be single-phase, and most (all?) global transactions 2-phase (phases in 2 separate epochs; frst phase reads+locks, second phase writes+unclocks); we won't do 3PC (well, unless there's a conflict, in which case: {add scheduled lock, read once prerequistes satisfied, write+unlock}). In part one we will read & add locks, in part two we will write and remove locks. how are these locks placed? what kinds of conflict can arise? and how do we resolve them when they occur?

Let each durable object hold a collection of locks, and own the semantics for lock scope and conflict. A lock could apply to rows or byte ranges within the object or whatever; the object owns such detail. Each lock takes the rank of the transaction it is associated with, and for a pair of locks that conflict the lower-ranked lock must be removed first (implicit DAG). On removal of a lock, new chain heads are identified, and the associated transactions execute if pre-requisites are satisfied.

There are three kinds of commit: regional, global P1, global P2.

Let's look at some example conflicts:

1. Two regional commits in same epoch conflict.
   - Fixpoint contract covers, lock-free; ordering and concurrency within epoch decided on VM by analyser that understands what writes are definitely associative, non-overlapping, etc. Partial analysis can optionally be shared along with log propagation to reduce amplification.
2. Regional and global commit in same epoch.
   - Impossible because admission separates regional and global epochs.
3. Two global P1 commits in same epoch conflict
   - Both add locks, one immediate and the other scheduled.
4. Regional commit conflicts with existing lock.
   - The regional commit adds locks for everything it depends on, as if it were a global commit.
5. Conflict between global P1 and p2 commit in same epoch
   - Process the p2 commits first.
6. Global P1 commit conflicts with P1 from previous commit
   - Impossible for the latest commit to have a rank below or equal to the previous commit rank. Add scheduled locks
   -
7. Coherent read across multiple shards (everything else I've shared is write-oriented)
   - Read request includes last global epoch; partial results returned cannot reflect state following application of a global epoch after this one (regional is okay; so if journal is {G0, G1, R0, R1, G2} and read specifies G1 snapshot, return value can legally reflect state as of any of G1, R0, or R1).

For a transaction like `RMW : (A_i, B_i) -> (A_i', B_i')` where A & B are different shards we might split this into 4 steps (lock+read A_i, send to B's journal; lock+read B_i, send to A's journal; write A_i'; write B_i')

I'm trying to think through all the classic softlock / deadlock scenarios in classical distributed programming and think we steer clear of them all(?). My suggested approach might struggle with backpressure reducing throughput, but that at least feels tractable. And I guess all discovery+reads are forced upfront... hmm, but what if we apply a set of locks, and while waiting, other writes change what set of rows match the predicate for the transaction in waiting? and are there other cases, like iterative transactions/sequences, where we might need more epochs to process a transaction. i'd like to find some way where we don't need N-phase transactions where N is arbitrary. i guess if it gets _really_ bad, you can force both phases of a global commit to run in sequence, causing a global slowdown... but that's really bad. what do we do?

Can you please work through a whole bunch of scenarios: historic and hypothetical cloud outtages as case studies, covering which my suggested approach could likely maintain availability through vs hit downtime on, and various disaster scenarios. And then various distributed scenarios that put the MVCC through its paces. And also propose an outline for the new brief.

---

I don't feel clear on continuations, either in the brief or spec. I think clearance and availability became conflated. Clearing an epoch just means the fold has been applied, producing new state; it doesn't say anything about the external availability of that state. Clearance should be dead fucking simple, so lets leave it out of the conversation that follows. Availability, continuations and transactions/locks have me in a genuine twist.

Let's say we have a transaction RMW : (A_i, B_i) -> (A_i', B_i'), where A_i and B_i are pieces of data from shards A & B. So we split this into four admissions, two for each shard:

J_A0. lock A_i and admit value into B
J_B0. lock B_i and enter value into A
J_A1. (depends on J_A0 and J_B0): run A_i' := RMW(A_i, B_i) and unlock
J_B1. (depends on J_A0 and J_B0): run B_i' := RMW(A_i, B_i) and unlock

Which is fine when considered in isolation. But other transactions are being admitted concurrently. What if we insert epoch J_R in-between J_A0 and J_A1 and it contains an admission that depends on A_i, but A_i is locked? Then what? The epoch can't clear? And so the whole journal deadlocks?

Upfront conflict-avoidant scheduling can't solve this, as knowing what rows are affected sometimes requires actually running the query.

So what do we do? I don't want you to immediately jump to an inelegant something pretending you've got a silver bullet. I want the options laid out and explored.

My thinking is the locks placed by J_A0 / J_B1 go on the object affected, and carry metadata like which items within the object are locked, which epoch will unlock them, and what's scheduled for retry after the lock lifts (not the entire J_R, just whichever transactions inside failed and need retry; logically, this retry becomes part of the epoch that unlocks). Something relatively simple-ish. We never need to add locks for transactions that complete within a single epoch.

What happens if A is able to lock A_i but B fails to get a lock on B_i? Is this that thing with people at a table picking forks up? Can you walk me through the thing with the forks and typical solutions?
