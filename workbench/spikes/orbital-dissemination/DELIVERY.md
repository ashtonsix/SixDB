# Delivery, joins, and obligations that outlive a route

2026-09-26. Candidate mechanisms for the dissemination spike, coordinated through
Orbital LEAD. These are proposals and bounded counterexamples. They do not add
guarantees to [Orbital's brief](../../../orbital/BRIEF.md), implement consensus,
or carry Calico's sender policy into SixDB.

Ashton's topology and algorithm ideas are prompts for comparisons, not selected
architecture. The concrete placements and mechanisms below are fixtures or
candidates. Their safety prerequisites apply when choosing that delivery
contract; they do not prescribe where every role must run or which graph solver,
sender policy, retry scheme or bootstrap protocol SixDB must use.

The useful invariant is **every required logical recipient is either complete,
still owed work, or explicitly released by the governing operation**. A tree is
one way to discharge those obligations. Its leaves, current VM count, transport
acks and disappearance of a failed route cannot define completion by themselves.

## Keep identities and completion boundaries explicit

A candidate obligation key is `(lineage, logical recipient, producer stream,
sequence)`, with operation/fragment identity where needed. It survives retries,
relay changes and VM replacement. The same key cannot name different canonical
payloads. A route version, transmission attempt, VM incarnation and packet number
belong to transport metadata. An executable deployment version may instead be
part of logical processor identity; that is an application lifetime decision.

| Boundary | What it establishes | What remains owed |
| --- | --- | --- |
| Received into a queue | A live process has bytes | Persistence, eligibility, execution and possibly return |
| Durable acceptance | A declared storage obligation covers the exact bytes | Admission and/or execution |
| Contiguous accepted frontier | All obligations in that lane through `n` were accepted | Effects may still be blocked |
| Chosen/admitted input | The applicable authority selected input | Payload readiness, fold, verification and publication |
| Internal effect completion | Effect and its completion record committed together under the modeled contract | Client return and external actions may remain |
| External sink receipt | Only the sink's specified receipt semantics | May not establish durable or irreversible completion |

Separately arriving payload, admission and optional plan enhancement must join on
exact input identity. Tentative bytes may be received and prepared; an ordinary
transport ack cannot authorize effects. Receiving sequence 8 cannot acknowledge
missing sequence 7. A proof upgrade for already received bytes need not resend
the bytes, but still needs an authenticated binding to them.

The ring `client → ingress → execution host → client` preserves the same
operation ID and declared completion event. A return address/capability, result
identity, expiry and authentication must survive ingress failure. A client
reconnects to a logical result owner or result store. An intermediate transport
ack does not prove that the final client received its response. Avoid coupling
the result-retention acknowledgement to the lifetime of the original connection.

The need for durable completion records, retry lookup after migration and
enforced stale-request handling has a concrete precedent in
[RIFL, sections 3–4](https://web.stanford.edu/~ouster/cgi-bin/papers/rifl.pdf).
Its result metadata travels with the associated object; its lease mechanism
rejects clients whose records may have been collected. That supports the lifetime
questions here, not adoption of its RPC interface or lease design.

## Failure is about the obligation, not just the socket

| Interruption | Candidate response | Safety boundary |
| --- | --- | --- |
| Target dies before durable acceptance | Retry another authorized holder using the same key | Original obligation remains outstanding |
| Target persists bytes, dies before applying | Recover the ledger and bytes; resume incomplete work | Receipt and effect frontiers differ |
| Target applies, reply is lost | Return retained outcome or suppress already completed effect | Completion record survives with the effect |
| VM replaced while delayed packets exist | Fence the old incarnation; resolve the stable recipient to the new owner | New owner imports the relevant ledger before serving |
| All copies of the delivery ledger disappear | Recover evidence or expose uncertainty/unavailability | An empty replacement ledger cannot safely forget surviving effects |
| Stream/result metadata is retired | Keep a durable rejection floor or fence the retired identity | An old retry must not become a fresh operation |
| Relay dies with batched frontier updates | Reconstruct/coalesce monotone claims upstream; resend within budget | Relay loss must not erase sole ownership of an obligation |

The executable counterexample for lost metadata executes one effect, creates an
empty replacement ledger with the same logical ID, retries, and obtains two
effects. Stable IDs alone are insufficient. The prototype assumes one durable
ledger survives a VM change, and its owner-generation check stands for an
externally granted and enforced fencing transition. It does not implement the
replication, transfer or fencing service.

Routes can overlap during updates. Preserve obligation IDs, allow old and new
trees to duplicate safe payloads, bound packet lifetime/hops to stop route loops,
and retain repair lookup until old obligations drain. Rejecting every old route
version discards legitimate in-flight messages; letting an old route grant new
authority is also incorrect. Historical authority and current service permission
need separate validation, as the
[recovery handoff note](../orbital-scenarios/recovery/HANDOFF.md) already explains.

## Choosing a sender in many-to-one and many-to-few

Pre-agreed entropy can remove a coordination exchange: hash stable message or
batch identity, destination group, membership/weight version and entropy version
into a weighted rendezvous ordering. A primary with backups in distinct failure
domains is one useful comparison against other redundancy policies. An agreed
entropy reservoir is a source of deterministic selection; consuming a shared
mutable random-number cursor is fragile under
reordering and loss. Counter-based derivation gives independent reproducibility.

Use the ordering to suppress redundant transmissions, never to grant exclusive
effect authority. Candidate-list disagreement can yield two primaries, or each
origin waiting for somebody else. A receipt-backed fallback policy must tolerate
both. Ranked hedges, fixed redundant senders and explicit sender ownership are
different tradeoffs; measure their extra traffic and tail reduction separately.
Every backup needs the bytes or a charged retrieval path. Two backups on one VM,
AZ, NIC, tunnel or saturated upstream do not supply independent protection.

Potential granularities include a message, a short sequence stripe, a batch,
destination subnet and query fragment. Per-message choice spreads independent
load but destroys batching locality; long stripes amortize metadata but make a
hot stripe and its failure more expensive. Include destination/fragment identity
in the hash so one epoch does not accidentally elect the same host for every
shard-to-shard edge. A topology update should not reshuffle all work in flight.

The implemented `sender_order` uses weighted exponential scores from stable hash
values and puts distinct-domain backups first. Its fixture distributes 256
identities across four candidates as 65/54/63/74. This is a deterministic sanity
check, not a fairness, tail-latency or load-balance result. Scheduling still needs
queue feedback, admission budgets, asymmetric prices and sender readiness.

## In-arborescence batching and aggregation

For reports certifying **the same producer stream's contiguous persisted
prefix**, pointwise maximum is idempotent and insensitive to message order:
`(A:p→7, A:q→2)` combined with `(A:p→9)` becomes `(A:p→9, A:q→2)`.
Preserve lineage, stream identity and the evidence needed to validate each range.
Combining the numeric maxima of distinct streams, or treating a highest received
packet as a contiguous persisted prefix, is invalid.

For reclaiming a shared payload needed by **all required recipients**, the
operation is the minimum of their qualifying release frontiers. A fast consumer
cannot release a slow consumer's obligation. For a partitioned query, completion
requires coverage of its declared fragments; the highest fragment number is
insufficient. Different frontier meanings must not share an untyped reducer.

Batching frontiers trades packet-processing work against timer delay and delayed
knowledge of progress. A relay can overwrite an unsent stream report with a
newer valid frontier, retain a dirty-stream bitmap, and flush on byte count,
oldest-item age or critical admission. Bound maps by active stream count; charge
hashing, coalescing, framing and evidence size, not just payload bytes. Recovery
and catch-up need their own fair share of relay capacity.

Admission/effect completion is also not arbitrary dataflow progress. If a relay
computes additional work, it may retain the ability to emit earlier outputs even
after its current queues empty. This resembles the outstanding-message and
capability accounting discussed by Naiad co-author Frank McSherry in
[Timely dataflow: reboot](https://www.frankmcsherry.org/dataflow/naiad/2014/12/27/Timely-Dataflow.html).
The inference for SixDB is narrow: a queue being empty is insufficient evidence
that no required earlier message can still be produced. The toy frontier reducer
does not implement general cyclic dataflow termination detection.

## Membership while messages are in flight

Define which history a joiner is owed. A new replica can install a snapshot at
cut `B` and receive the subsequent tail; an ephemeral subscription may begin at
an agreed future cut. Neither implies receiving all historical messages.

For snapshot plus tail, one candidate captures a valid snapshot root at `B`,
pins its dependencies, and registers a tail-retention obligation starting at
`B+1` in one logical transition. A staged handshake or overlap scheme could
establish the same gap-free coverage; the prototype assumes the atomic version
to isolate its delivery costs. Snapshot transfer can then proceed while new
traffic arrives.
Buffer or durably accept the tail, track gaps, install the exact snapshot, and
declare the joiner ready only at its required contiguous catch-up cut. For
multi-stream state, `B` is an application-valid cut/vector with dependencies,
not independent maxima chosen from unrelated streams.

The membership obligation must exist even when an old tree has already captured
its recipient list. Replay/anti-entropy finds the new recipient's missing tail;
the old tree need not be rebuilt for every in-flight message. Dissemination can
use a logical subnet gateway while the gateway owns local membership. That
delegation needs a precise coverage acknowledgement and a recovery owner; a
gateway ack cannot silently mean only that the gateway received the bytes.

An arbitrarily slow join and finite retention cannot coexist with unlimited
admission unless another storage tier absorbs the tail. The available choices
include backpressure, a charged retained-log tier, restarting from a newer valid
snapshot, or explicit failed subscription. Silent eviction cannot be reported as
successful catch-up. A failed or restarted join also needs explicit release of
its old retention pins. Existing required replicas cannot simply be relabeled
as failed optional subscriptions to improve the completion fraction.

At 1 million 1-KiB events/s, a 100-ms uncompacted lag consumes about 102.4 MB of
payload retention before framing, indexes or replicas. This is arithmetic, not a
measured capacity. Shared immutable retention need not multiply payload bytes by
recipient count, although per-recipient gaps, receive buffers and copied batches
can. Storage credits and receiver credits should be separately visible.

## Read scale-out and executable applications

The same transport supports different logical recipient contracts:

| Work | Required logical completion | Failure/duplicate consequence |
| --- | --- | --- |
| Replica broadcast | Each required replica reaches its declared frontier | Repair owed replicas; all-recipient completion may lag effect latency |
| Read sent to any replica | One eligible result at the required snapshot | A hedge may win; suppress duplicate result consumption |
| Scan split within one shard | Every logical fragment completes once in the assembled result | Retrying a fragment must not double-count its rows/aggregate |
| Query scatter across shards | Coverage of the query's captured shard/range map | Shard split/move cannot create omitted or overlapping fragments |
| Extension pipeline across roles | Parent operation's required inputs, checks and effects | Early local communication does not remove distributed obligations |

Keep `(query, snapshot/cut, logical fragment, attempt)` separate: attempt is
execution metadata, while a stable fragment identifies result deduplication and
coverage. Hold snapshot/dependency pins through the relevant consumption or
retry lifetime. Data-dependent early exit can release remaining work only under
the query's declared semantics. Cancellation is not proof that an earlier
attempt produced no result or effect. Internal relay plans must not promote a
tentative result or relax required extension verification.

For local 100-ns-style memory transfers, copying may disappear while ownership,
backpressure, publication fences, ring wrap and stalled-core behavior remain.
That latency is a user scenario, not a measured complete operation. A cross-AZ
fallback for an overloaded local consumer can change both service cost and the
location of retained results; price the complete recovery and return path.

## The tentative witness-proposal candidate

The clarified benchmark placement has client/producer `P` and witness leader `L`
in AZ A; the fast witness follower `F` in AZ B is also a producer follower. `L` does not
hold the payload. The consumer can be in AZ B. Compare the existing strict
eligibility-before-proposal path with a research candidate that overlaps
`P→L→F` tentative journal propagation and `P→F` payload propagation.

This is one way to expose dependency overlap, not a requirement to use separate
tentative journal and payload paths. Different assignments of producer and
witness roles, payload-bearing journal records, or a combined persistence
operation could make prerequisites meet elsewhere or remove a separate transfer.
They would change bytes, persistence contention, failure independence and which
evidence a follower can propagate. The invariant is to preserve the applicable
payload durability and chosen-history evidence for every choosing quorum and
recovery path; an implementation need not reproduce this fixture's labels or
number of writes. The current comparison establishes no winner among those
unmodeled alternatives.

An early candidate certificate must bind the **exact entry and every included
event's payload digest**, complete durable journal writes from its qualifying
witnesses, and two independent-domain durable payload copies with retention
obligations. `F` must wait for both its required journal write and payload write
before the combined certificate can justify downstream admitted effects. A bare
`L+S` journal pair cannot qualify when slow witness `S` has no payload evidence
and only `P` holds the event. `S` need not hold payload itself if independently
valid retained `P+F` evidence covers the entry; voting and payload roles differ.

This changes the interpretation of early journal records. If `L`'s tentative
record were already counted as an ordinary unconditional vote, an ordinary
two-witness recovery might choose it without the missing payload condition.
Calling it tentative in a simulator does not fix that protocol. Every choosing
quorum, recovery path, prefix extension and reconfiguration must preserve and
enforce the condition. A leader reboot cannot turn its old ineligible tentative
proposal into eligible work merely by retransmitting it. Interrupted writes do
not provide receipts; a receipt for a different payload or an unretained cache
copy cannot close the condition. Historical chosen evidence must remain valid
under its original configuration; later loss of a payload does not unchoose it.

There is also a shared-prefix stall: tentative slot 1 can await missing payload
while an independent producer's slot 2 has complete evidence. A contiguous
admission prefix cannot advance through slot 2. The probe retains that concrete
history. Postponing slot allocation, restricting a tentative window, separating
staging from accepted history, and prioritizing repair may limit the damage, but
each changes scheduling or protocol state and needs its own analysis. Replacing
the earlier slot with a no-op is not a free escape if it may already be chosen.

The [observed proposal-timing comparison](PROPOSAL-TIMING.md) now explores one
bounded scheduling policy: actual payload replies, local receipt deadlines and
received warnings select early or strict timing for future operations. Its
[checks](check_proposal.py) retain an old missing-payload hole after switching,
keep each in-flight operation's original choice and charge receipt/ACK traffic.
A small early window limits unanswered samples, not how long an allocated gap
can block. The policy neither diagnoses the failure cause nor changes conditional
eligibility, recovery authority or publication requirements.

`qualifies_early_certificate` checks only these supplied-fact guards, including
same-ballot exact-entry quorum, exact event/digest coverage, domain independence,
and retention promises. It cannot establish that the votes were issued legally,
recover an accepted prefix, enforce retention, authenticate receipts, or compose
conditional acceptance with consensus. Membership transfer remains owned by the
[recovery investigation](../orbital-scenarios/recovery/README.md). This candidate
is not ready for adoption on the basis of this helper or latency simulation.

## Retry cannot complete every external action safely

For an irreversible sink lacking idempotency or outcome lookup, the histories
“sink acted; reply was lost” and “sink never acted” can leave identical local
state. A marker before send preserves at-most-once dispatch but can omit the
effect after a crash; a marker after send permits duplicate effects on retry.
The probe exhibits zero and two effects respectively. Retrying with the same
network message ID does not make the external sink deduplicate.

An external effect contract may instead use a sink's atomic idempotency key and
result record, a transaction spanning the sink, queryable operation status, or
explicitly accepted uncertainty/manual reconciliation. A committed intent is
the durable source of an owed action; it is not evidence that the action happened
exactly once. At-most-once data-loss-tolerant processors need their own lifetime
and omission semantics. Do not quietly upgrade them to retrying processors.

This failure ambiguity is also explicit in
[Birrell and Nelson's RPC paper, section 3.1](https://www.cs.cmu.edu/~15712/papers/birrell84.pdf):
failure does not always let a caller distinguish whether the remote procedure
ran. A ring changes where the response originates, not that information limit.

## Executable scope and performance integration

Run from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_delivery.py \
  --output build/orbital-dissemination/delivery-checks.json
```

The **26 authored checks** include 12 duplicate/reorder histories, five crash
boundaries, six snapshot/tail permutations, 60 frontier-report permutations,
incarnation fencing, route overlap, tentative-input gating, finite queue/tail
exhaustion, stale retry rejection, sender disagreement, correlated-domain backup
selection, external-effect ambiguity and six conditional-certificate/prefix checks.
The JSON includes source hashes. It is regenerated in ignored storage.

The [model](delivery.py) uses atomic Python transitions and trusted durability,
admission, snapshot and ownership facts. It covers one stream per delivery lane
and a single-stream retained log. Audit effect counters are unbounded test
instrumentation outside the finite modeled ledger. It does not implement disk
crash consistency, arbitrary Byzantine inputs, packet repair, a failure detector,
real timers, liveness under permanent faults, or the authority protocols. The
authored histories are not exhaustive exploration of a distributed implementation.

### Network-backed membership scenario

[membership_scenario.py](membership_scenario.py) connects the retained-log model
to the shared `Sim`/`Network` resources. Its fixed foreground route is `p→c`;
at `join_us`, the logical subscriber `j` is added and captures the current durable
source barrier. The physical node is already represented in the topology so its
links and faults can be configured. Snapshot chunks and repairs compete with
foreground traffic for real modeled CPU, NIC, fabric and persistence queues.

The source releases tail retention only after **application durable ACKs return
over the network**. Transport ACKs do not advance it. Snapshot chunks persist at
the joiner, then snapshot installation performs compute and a metadata write;
the installation ACK is separate. Missing-ACK timers drive bounded snapshot and
repair windows and bounded application attempts, on top of transport retries.
The sender does not inspect target state, packet-loss outcomes or receiver queues
to decide which data to repair. Retained tail plus reserved source writes consume
finite source credits; exhausted credits explicitly refuse offered traffic.

Run the five bounded fault cases or a scenario configured by a JSON object:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_membership_scenario.py \
  --output build/orbital-dissemination/membership-checks.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/membership_scenario.py \
  --output build/orbital-dissemination/membership-default.json
```

Use `--config PATH` for a JSON object, or import `run_membership(config)` for a
programmatic sweep. Controls include `count`, `rate` (events/s), `size`, `join_us`,
`snapshot_bytes`, `snapshot_chunk_bytes`, `snapshot_window`, `repair_window`,
`repair_poll_us`, `application_retry_us`, `application_max_attempts`,
`retention_events`, `drain_us`, and the shared network configuration. `faults`
passes directly to `Network.inject`; for example a directed partition is
`{"kind":"partition","src":"p","dst":"j","at_us":110,"until_us":600}`.

The five checked histories cover an omitted joiner in the old route, a temporary
forward partition, a joiner crash/recovery, finite-retention exhaustion, and
permanent reply-path loss. In the small exhaustion fixture, 16 of 80 offers are
accepted and 64 explicitly refused; all accepted events eventually reach both
required consumers. Under permanent `j→p` reply loss, 16 retained obligations
remain, 60 of 80 offers are refused, and the source never reports the join ready.
These are synthetic bounded scenarios, not service-rate recommendations.

Outputs include readiness and missing tails at workload cutoff and after drain,
source credit/retained/pending high-water marks, foreground durable-delivery
latencies, refused offers, application retry exhaustion, snapshot/repair payload
attempt bytes, transport wire bytes/prices and per-resource occupancy. Snapshot
validity and pinning, authority, and survival of completed durable receiver state
across a process crash are assumptions. Source storage loss, snapshot dependency
fetches, replicated retention, changing logical identity, and full shard admission
remain outside this scenario. A source write lost before completion stays visibly
reserved/blocking; it is not magically reconstructed after a source crash.

For the performance simulator, charge independently: received/duplicate packets,
accepted/rejected bytes, persistence work, old-route traffic, repair traffic,
sender/receiver queue occupancy, oldest missing obligation, snapshot/tail pins,
ledger/gap metadata, hedges cancelled too late, and network price by edge class.
Report offered, admitted, completed, failed, backpressured and unfinished work.
Keep payload-ready, admitted internal effect, all-required-recipient coverage and
client-return latency separate. Selectively finishing easy obligations can improve
p99 while making the service worse; finite-run completion fraction is essential.

Useful fault compositions are loss plus relay overload; target death after effect
with reply loss; old/new route overlap during receiver join; shared-AZ loss of
selected senders; query-fragment reassignment during shard movement; snapshot
copy competing with the live tail; and a stalled required consumer exhausting
retention while witnesses remain healthy. FEC or packet retransmission can repair
some bytes. They cannot create missing authority, snapshot validity, deduplication
history or external-effect knowledge.
