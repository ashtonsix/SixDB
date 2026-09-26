# Reads, extensions and useful relay work

2026-09-26. A broad contribution to the dissemination spike, owned for integration
by Orbital LEAD. These are candidate mechanisms and authored examples, not an
extension of the [current brief](../../../orbital/BRIEF.md), an implementation
contract, or measured network performance. Read meaning remains application-owned;
Loom owns scheduling and Orbital supplies transport and durable coordination.

The main opportunity is to transport work to suitable resident data, combine
results before expensive edges, and reuse analysis where its saved work repays
its distribution. The main danger is confusing several different obligations:
serving a query once, reproducing a deterministic state fold, verifying extension
execution, and retaining enough history for recovery. Query work can be split
among replicas without silently dropping the other obligations.

The earlier [large-read survey](../orbital-scenarios/reconsideration/READS.md),
[locality counterexamples](../orbital-scenarios/reconsideration/LOCALITY.md),
[extension composition](../orbital-scenarios/reconsideration/COMPOSITION.md) and
[worked ELT histories](../orbital-scenarios/ELT-WORKED.md) retain the semantic
cases. This note adds their transport and resource consequences. In particular,
a pending WAN transaction must not become an epoch-wide delivery or publication
barrier for independent local results.

## Workloads worth separating

| Workload | Useful candidate | Cost or correctness condition that could reverse the result |
| --- | --- | --- |
| A point read from one shard | Select one replica that already serves the required cut; return directly to the client or its ingress | A nearby replica may be stale, cold or overloaded. A shorter request path is not necessarily a shorter completion path. |
| A batch of keys across shards | Group by shard, then by destination host; bounded packet batches and independent per-key completion where allowed | Batch latency follows the slowest required key. Per-key output cannot silently replace atomic whole-query output. |
| One large scan within a shard | Partition ranges or execution chunks among resident replicas | Each replica must serve the same cut. Splitting work is useful only after dispatch, decoding, combining, memory bandwidth and interference are charged. |
| Many small reads within one shard | Assign whole queries across replicas; share immutable decoded blocks and compiled code where possible | Splitting every point query can spend more on coordination than on reading. Cache affinity and tenant concentration matter. |
| A report spanning many shards | Scatter to source-local workers, reduce at host/AZ/region frontiers, stream a coherent result | Missing partitions are not empty partitions. Fanout compounds individual tails; reducing bytes need not reduce packet count. |
| A large join | Repartition keys at a chosen exchange; broadcast a genuinely small side; prefilter when selective | Hot keys create concentrated output and CPU. A small build side replicated to many workers may exceed the cost of one partitioned copy. |
| Exact top-k, count, distinct or percentile | Local partial results with a specified merge algebra | Exact top-k over disjoint rows with one total tie order can send at most k candidates per partition. Exact distinct and percentile do not generally have fixed-size exact summaries. Joins can invalidate a pushed-down top-k. |
| Dynamic graph traversal | Move continuations toward data; group next-frontier requests by host and shard | New sources must serve the same cut. A temporarily empty queue does not prove traversal completion; cycles and duplicate visits need query state. |
| A long report, pagination or slow client | Retain one cut, stream bounded pages, separate result generation from delivery credits | Result buffers and retained versions accumulate. A new cut per page changes the answer. A disappeared client must not pin history indefinitely without policy. |
| A large read followed by narrow writes | Compute close to inputs at the fixed transaction position; ship compact outcomes | Read-bound registration, pending effects and atomic output installation remain required. Read scale-out does not remove declared dependencies. |
| A significant backend inside extensions | Fuse local request/response stages, keep immutable intermediates on-host, send continuations instead of repeated input tables | Sandbox transitions, required native checks, interaction logs, runtime/code versions and output expansion can dominate the saved RPCs. |
| One extension calls another shard and returns to a different server | A ring of request, execution and direct result delivery | Response ownership, capability checking and client connectivity must survive the original ingress disappearing. An internal call must not wait for its enclosing transaction to publish. |
| Change subscriptions, derived features and cache invalidations | Coalesce frontier progress and share base deltas per destination host | Superseding a value is safe only under the subscription's semantics. Dropping intermediate notifications can lose required effects or counts. |
| Cold analytics replicas or newly added consumers | Bootstrap a retained cut while current work continues, then replay to a stated serving frontier | Starting network delivery does not establish readable state. Catch-up needs CPU, object bytes, code/dictionaries, validation and bounded repair bandwidth. |
| Several colocated shard roles per VM | Aggregate host-to-host envelopes; execute local edges through memory queues | Many 100ns edges still share CPU, caches, memory controllers and queue capacity. One VM fault removes several roles together. |
| Shared witnesses and a few hot tenants among many shards | Isolate ready admission/control traffic from scans, verification, catch-up and result streams | Witness placement optimized for one shard can overload shared packet processing or log service. Physical resource isolation and logical independence are separate. |
| Regional consumers feeding each other and external clients | Regional gateways plus local adaptive routing, selective replication or materialized outputs | Regional gateways add failure concentration, hop latency and control state. External result paths may need an existing client session instead of direct delivery. |

These scenarios combine. A tenant can submit a tiny request that causes a
cross-shard scan, emits a huge intermediate join, performs a small mutation, and
returns a few bytes through a different server. Offered request rate and request
size alone will miss its resource demand.

## Read coverage and replica readiness

A useful work description carries a query/invocation identity, semantic source
cut, plan/executable identity, logical source partition or range, and output
meaning. Scheduling attempt and physical worker identities are separate. A
replacement attempt covers the same logical work; it does not add a new shard
or count twice toward completion. An expected-domain certificate may be a compact
range set or versioned partition map rather than one record per chunk.

The serving contract needs to distinguish a replica's received journal frontier,
fold frontier, resolved relevant effects and retained readable versions. The
fastest network arrival is not necessarily the first usable read. For the brief's
updating transactions, a distributed execution at position `c` still registers
the required dependency bounds before reading or waiting. Relaying a cached value
cannot skip those obligations. Read-only work uses the application-selected
retained cut; independently choosing the newest local shard epoch is insufficient
for a coherent multishard result.

Replica membership and logical data membership are different changes. Adding a
worker can help cover an existing query domain after it obtains the required
state. It does not retroactively create an additional logical range to read.
Repartitioning logical ranges needs an old-to-new coverage mapping valid at the
query's cut, with no gap or double count. A dynamically discovered source must
be added to the query's actual dependency/work domain, with termination evidence
for further discovery. The network's current list of live VMs cannot stand in
for either obligation.

For streaming results, receiving an end marker before a missing chunk does not
complete coverage. A frontier can compress knowledge of contiguous completed
ranges, but cannot assert an absent range complete. Count result identities and
coverage, not the number of replies: two replicas returning the same shard leave
another shard missing. If equivalent attempts disagree, discard-by-identity does
not establish which result is correct. The finite [probe](read_probe.py) exercises
these counterexamples; it does not implement range-map transitions or prove the
distributed cut protocol.

## How to use replicas without multiplying all work

Use different controls for query concurrency and within-query parallelism. For
many point reads, whole-query placement may amortize dispatch best. For a long
scan, distribute bounded chunks among replicas that already hold the necessary
columns and versions. Increase the number of workers only while marginal service
gain exceeds dispatch, combining, cache disruption and the interference imposed
on other traffic. Full fold replicas remain a separate background cost.

Candidate scheduling policies include contiguous range affinity, weighted static
partitioning, a host-local chunk queue, and bounded work stealing from the
remaining ranges of a straggler. Compare AZ-local-first stealing against remote
idle capacity, including the transfer price; moving a small continuation to
resident data may be cheaper than moving large state. Late stealing needs explicit
split boundaries: do not rescan a whole
shard merely because one last range is slow. Persisting every scheduling choice
would add another cost and is unnecessary when scheduling changes no semantic
result; accepted outputs, runtime versions and recovery requirements still need
their ordinary records.

Morsel-driven execution supplies a useful local precedent for runtime adjustment
of parallelism and NUMA-aware assignment. Extending that idea across hosts adds
transport, replica readiness, failure, retention and coverage obligations that
the local mechanism does not supply. [Leis et al., Morsel-Driven Parallelism](https://db.in.tum.de/~leis/papers/morsels.pdf).

Smaller chunks permit faster load correction and cancellation but incur more
dispatch, bookkeeping and tiny messages. Larger chunks amortize work but increase
straggler loss and nonpreemptible service. Useful chunk size depends on the
operator and representation: a compressed block, hash bucket and UDF invocation
have different splitting costs. A pipeline breaker or one enormous hot group
may be indivisible without changing the operator. Shipping fewer bytes by
reusing a resident hash table can justify a deliberately uneven allocation.

Load balancing should estimate remaining completion cost: queue service, relevant
snapshot readiness, resident data/code, predicted output, merge destination and
link price. Compare locality-weighted routing, two-choice probing, deterministic
weighted assignment and feedback-controlled placement as candidates. Noisy
arborescence edge weights alone do not model hot join keys, a shared memory
controller or a replica still catching up. Hash-based stable assignment helps
limit migration, while per-request correction can respond faster than whole-tree
distribution. Their control cost and convergence under shifting flows need
measurement.

The degrees of freedom are deployment-wide. A VM serving shards A, B and C can
be the best local consumer for all three individually and an overloaded choice
jointly. A read worker using the same CPU pool as the fast durable follower may
damage the post-persistent-effect tail even when average CPU utilization seems
comfortable. Retain mixed read/write scenarios and completion-class counters.

## Reduce and exchange at useful frontiers

Dremel demonstrates a serving tree that combines partial query results before
they reach the root. Its read-only aggregation design is useful precedent for
reducing network volume; it supplies neither SixDB's updating semantics nor a
universal optimal tree. [Dremel, section 6](https://research.google.com/pubs/archive/36632.pdf).

Place reductions where inputs naturally meet: thread to host, host to AZ, AZ to
region, then only the necessary output across an expensive edge. A reduction
tree may differ from the epoch dissemination tree because its byte sizes and
completion dependencies differ. Operators include exact integer sums/counts,
min/max with deterministic ties, sorted merge and mergeable application-owned
summaries. Some need substantially more state than the final answer. Floating
point addition is not associative under ordinary arithmetic; arbitrary relay
groupings need a specified deterministic accumulation method or fixed logical
order. A UDF cannot be assumed reducible merely because its output is small.

Partial results need provenance sufficient to distinguish a disjoint contribution,
a retransmission and a superseding partial result. If a worker resends its running
sum, adding both versions doubles part of the input. Candidates are immutable
range contributions, replacement values keyed by covered range, or deltas with
deduplicated identities. Once contributions have been combined, preserve enough
coverage lineage to repair missing work without counting the same range twice.
This state is part of the reduction's cost.

For many-to-many joins and group-by exchanges, group output by destination host
and coalesce small items while preserving per-query or per-lane credits. A host
envelope can carry several shards' independent records without creating one
semantic barrier. Compare scatter from every core against host-local combination
before sending. Tiny skewed keys can be compute hotspots despite small byte
counts; large values can saturate links with few packets. Salted heavy-key work
splits or replicated small join sides are candidates only where the merge rules
preserve duplicates, unmatched rows, ordering and atomic outputs.

Progress aggregation can use coarser frontiers where losing some concurrency is
cheaper than sending every update. Naiad explicitly studies local and hierarchical
accumulation of progress updates, including safe flush conditions; delaying the
wrong updates can delay completion. This is useful evidence for the tradeoff,
not permission to map its pointstamp protocol directly onto witness admission.
[Naiad, section 3.3](https://www.microsoft.com/en-us/research/wp-content/uploads/2013/11/naiad_sosp2013.pdf).

## Ring paths and backend extensions

Model requests as a dataflow graph. A client can submit to A, A routes to B near
the source, B calls a shard served by C, and D delivers the final result. Request
and result routes have different sizes, readiness times and useful endpoints.
An ingress need not remain in every response path, but the client must have an
authorized reachable return route. External clients behind session state or NAT
may require an existing gateway; an internal extension may receive a direct
memory-queue delivery. Do not assume direct unsolicited packets work everywhere.

Carry stable invocation/result identity and the required return capability across
the ring. A transport attempt ID and current locator can change independently.
Separate accepted request, admitted input, computed tentative output, committed
outcome and client-received result. Losing A after B accepted the request should
not force the client to create a new semantic transaction. A result from D can
be retrieved through a replacement route when its status or reconstructible
outcome survives. For a read, exact recomputation may require retaining its cut
and code; keeping neither can leave an unavailable answer even if the database
remains healthy. The retention cost belongs in the comparison.

Duplicated delivery is not authority to duplicate external effects. Backend
extensions that send email, issue payments or call external systems still need
the brief's committed-intent dispatch and an external idempotency/status contract
where available. After an ambiguous external outcome, an automatic replay is not
a general recovery strategy. Transport can repair delivery of the recorded
intent or result; it cannot infer whether an uncooperative external system acted.

Place extension stages with data, executable code, required validation and output
size in mind. Three comparisons are especially useful:

- Ship raw inputs once to a shared compute stage, then send a small derived
  result. This saves repeated computation but creates a compute hotspot and may
  require independent verification elsewhere.
- Ship canonical inputs to consumers and derive local state. This can reduce
  network bytes when state and indexes expand, while increasing CPU and cold
  recovery work. Count all mandatory executions.
- Materialize a commonly used intermediate near its readers. Include its update
  fanout, version retention, invalidation, ownership and low-hit-rate waste;
  materialization is not automatically cheaper than recomputation.

For a significant backend invocation, avoid copying a large intermediate between
every internal service boundary if a sandbox-safe local reference suffices. Its
lifetime, isolation and copy-on-write cost remain real. Relay a continuation with
captured inputs to another shard when locality justifies it; do not move mutable
private process memory as if it were an agreed fact. Checked native execution
must meet the current all-required-executions match rule even for read-only
outputs. A faster unchecked replica result cannot substitute for that rule.

## Message enhancement and admission/payload joining

Enhancement is broader than a plan hint. A message may carry exact matching row
IDs or a bitmap, computed values, partition assignments, partial aggregates or
other reusable work. The profitable representation depends on selectivity,
input residency, row mapping, receiver compute and edge price. Binding the
enhancement to the correct input/cut, predicate and result meaning is essential;
an exact selection cannot be silently replaced with a probabilistic filter.
[ENHANCEMENT.md](ENHANCEMENT.md) explores this selective-filter example. The
following plan-sharing example is one instance of the broader transformation.

A consumer near witnesses may receive an epoch and producer payloads early
enough to compute useful independence analysis or a partial application plan.
Other consumers can reuse it if they can validate its applicability cheaply.
The useful identity covers the input ranges/digests, application scopes,
plan/executable/runtime version, and any state or schema on which the analysis
depends. Absence of a dependency is a semantic statement, not a prediction based
on which messages happened to arrive first. A relay with only half the events
cannot assert independence from the missing half.

Compare three concrete policies:

1. Forward immutable admission and payload pieces as soon as each is eligible;
   every destination computes its plan when its own prerequisites are present.
2. Join inputs at a relay, attach the plan, then forward the enhanced message.
   Charge waiting, join-buffer memory, analysis CPU, per-edge expansion and the
   extra common failure point. Do not let a remote slow input hold all unrelated
   epochs or transactions at this relay.
3. Forward the raw pieces and emit a separate optional hint when ready. Receivers
   use an applicable hint if it arrives in time; otherwise they compute locally.
   Charge duplicated work, extra packets and sidecar deduplication. The optional
   path provides no worst-case CPU saving guarantee.

A per-input hold deadline is a transport policy: after it expires, forwarding
raw data must preserve the same logical outcome as waiting. A partial plan may
cover an explicit prefix or dependency-closed subset and leave unresolved work
to receivers. Hint absence, relay failure, graph change or version mismatch
should not prevent an otherwise complete raw execution unless the application
explicitly made the shared result authoritative and retained its recovery path.

The opportunity depends on where the prerequisites meet. A relay may finish
analysis during time a downstream consumer is still waiting for its epoch;
then shared analysis can improve that consumer's completion as well as save
CPU. A consumer already near the fast follower may have no such slack. An
increased packet crossing the public internet can lose far more than the same
packet within an AZ. Share a hint within an AZ, compress it to a versioned
identifier across a slow edge, or omit it for consumers where validation and
transport cost exceed local analysis. A cache miss needs a raw fallback or a
charged fetch; a hash is not the plan itself.

Joint optimization is therefore over placement, representation and scheduling,
not just edge selection. A graph representation can introduce explicit internal
compute nodes: input readiness feeds analysis, analysis produces extra bytes,
and the enhanced flow occupies real outgoing resources. Choosing a network tree
before deciding which edges carry enhanced data can select the wrong objective.

## Accounting and falsifiable small cases

[read_probe.py](read_probe.py) provides exact finite coverage cases and authored
analytical comparisons. It deliberately has no packet queues, arrival process,
fault simulator, storage or durable critical path; the main simulator owns those.
It does not establish the requested 170us p99 / 250us p99.9 post-persistent-effect
target, and that write target is not a general bound for scans or client responses.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_read_probe.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/read_probe.py \
  --output build/workbench/orbital-dissemination/read-probe.json
```

Eight checks pass. The results below are derived from supplied numbers, not
timings of these Python programs or estimates for real hardware.

**Replica availability.** One million bytes, four independent workers each
processing 1,000 bytes/us, no dispatch or merge overhead:

| Authored availability | Best whole-replica read | Equal partition | Divisible-work lower bound |
| --- | ---: | ---: | ---: |
| All four ready at 0us | 1,000us | 250us | 250us |
| Three ready at 0us; fourth ready at 400us | 1,000us | 650us | 333.33us |

In the second case the bound assigns no bytes to the late worker. An actual
dispatcher pays chunks, queues and merge cost, so this is an optimistic reference
for allocation, not a scheduling result. Partitioned execution scans 1MB total;
four independent full executions scan 4MB. Mandatory state folding and verification
remain outside these query-only counts.

**Fanout tails.** With independent component completion probability `p` by a
deadline, N required pieces finish by it with probability `p^N`. If each of 128
pieces meets a deadline 99% of the time, the whole query meets it only about
27.6% of the time. To meet 99.9% jointly under independence, each needs about
99.999218% success at that deadline. Without independence, allocating failure
budget `(1 - target) / N` per piece gives a sufficient union-bound budget,
but may be conservative. Correlation changes the answer: a shared failure can
defeat every hedge even though fully correlated component delays do not have
the same maximum distribution as independent delays. Tail-aware redundancy and
load balancing deserve evaluation with joint traces. [Dean and Barroso,
The Tail at Scale](https://research.google/pubs/the-tail-at-scale/).

**Hedges and cancellation.** Two equal dedicated workers each needing 100us of
CPU, launched together, finish in 100us and spend 200us of CPU. No latency is
saved. An authored 1,000us primary with a 100us secondary launched at 50us
finishes at 150us; immediate cancellation spends 250us CPU, while a 200us
cancellation delay spends 450us. These are fixed service examples, not estimates
of hedge behavior under queues. In the real model, charge queued attempts,
cancel messages, work already sent and completed-but-undrained results. Reserve
a bounded hedge budget and prefer genuinely distinct failure/resource paths;
hedging into the same overloaded host is not independent redundancy.

**Hint placement.** Assume an 8us local analysis or a shared 8us analysis plus
1us validation at each consumer. Relay epoch/payload readiness is 20/10us, and
the sidecar is 4KiB. The table holds raw input routes fixed and adds ideal
sidecar serialization; it omits packet framing, shared sender queues and memory
movement. Price weights are abstract relative units, not cloud prices.

| Consumer | Raw epoch/payload ready | Sidecar propagation; rate | Local finish | Wait-for-hint finish | Hint weighted bytes |
| --- | --- | --- | ---: | ---: | ---: |
| Fast AZ | 22/12us | 2us; 12,500B/us | 30us | 31.33us | 4,096 |
| Epoch-late AZ | 60/20us | 20us; 12,500B/us | 68us | 61us | 4,096 |
| Public edge | 60/20us | 20us; 125B/us | 68us | 81.77us | 409,600 |

Across these three consumers, waiting for shared analysis reduces total modeled
analysis/validation CPU from 24us to 11us while making two completions slower.
The optional-sidecar minimum in the probe is only a completion lower bound;
it assumes freely choosing the faster applicable path and does not promise the
same CPU saving. Holding the raw stream at the relay can be worse still. At
250,000 messages/s, adding 4KiB adds 1.024GB/s per copy before framing; tiny per-
message convenience can become a substantial sustained flow.

For the full simulator, retain at least useful results and completed domain
coverage, actual scanned/decoded bytes, CPU service by stage, all emitted bytes
and packets by edge class, memory copies and resident/retained bytes, duplicates
and cancelled work, missing results, rejection and backlog. Price bytes on
physical edges; do not count same-host memory edges as internet traffic. A
100ns assumed memory handoff still needs explicit queue, producer and consumer
service, cache coherence and finite capacity. Summing edge percentiles or timing
only completed queries can hide the failure being investigated.

## Scenarios still requiring the main simulator or measurement

The analytical cases make no claim about saturation. The discriminating network
runs combine a scan with the short durable path, uneven replica cut readiness,
a high-output join, shared-witness traffic, a ring result endpoint failure, a
slow/cancelled client, and a relay losing one of its two input streams. Sweep
payload and hint size, chunk size, packet batches, offered load, skew, sharing
of CPU/NIC resources and correlated delays. Report offered/completed work and
unfinished obligations alongside quantiles. For dynamic membership, distinguish
adding a physical executor, adding a new subscription obligation and moving a
logical shard boundary; they require different coverage evidence.

The most useful next real measurements are per-packet CPU cost, same-host queue
handoff under competing cores, operator analysis/verification cost, whole-query
versus partitioned scan service, and the joint epoch/payload arrival distribution
at candidate relays. Those measurements can calibrate the model's competing
paths; selecting a shared plan, an adaptive tree family or a universal read
fanout width before them would overstate the evidence.
