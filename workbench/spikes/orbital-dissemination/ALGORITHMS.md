# Routing, aggregation and work placement

2026-09-26. An algorithm contribution to Ashton's dissemination investigation.
These are candidate mechanisms and authored counterexamples, not additions to
the [Orbital brief](../../../orbital/BRIEF.md). The companion
[planner](routing.py) computes small static graphs. Its checks establish those
computations, not transport safety or a latency target.

The useful question is the cost of satisfying each actual obligation. Receiving
an epoch, possessing its inputs, finishing a verified fold, triggering a
post-persistent effect and returning to the initiating client are different
events. Routing every message to every VM would discard this distinction.
The [retired study's lessons](../../notebook/consensus-networking.md) are relevant:
count unfinished work, trace dependencies and charge shared resources.

## What a tree optimizer does and omits

A rooted out-arborescence reaches each vertex through exactly one parent. The
minimum-cost version minimizes the **sum of selected edge weights**. Edmonds'
original paper specifies that objective; neither path duration nor sender
fanout is part of it. The implementation here contracts a cheapest-incoming-edge
cycle, solves the reduced graph and expands it. It supports asymmetric links,
parallel physical flows, zero weights and negative finite planner penalties.
[Edmonds, *Optimum branchings*, 1967](https://nvlpubs.nist.gov/nistpubs/jres/71B/jresv71Bn4p233_A1b.pdf).

An exact small example separates cost from latency. There are six destinations.
Each link along `r→1→2→3→4→5→6` costs 1 and takes 1 µs. Direct `r→2` through
`r→6` links cost 1.1 and take 1.1 µs. With unlimited simultaneous transmission:

| Planner | Total edge cost | Depth | Maximum fanout | Last destination |
| --- | ---: | ---: | ---: | ---: |
| Minimum arborescence | 6 | 6 | 1 | 6 µs |
| Shortest paths | 6.5 | 1 | 6 | 1.1 µs |

Extending the chain makes the delay ratio arbitrarily large while retaining a
small per-edge saving. Conversely, when `r` serializes six payload copies through
one saturated NIC, the star's apparent concurrency disappears. Neither endpoint
is a general winner. These exact graphs and their outputs are in
[check_routing.py](check_routing.py); their times are authored constants.

Edmonds remains useful as a cheap-network baseline and a candidate generator.
It is an unsuitable final objective for the requested p99/p99.9 effect boundary.
A graph with every possible relay vertex also forces all those vertices into a
spanning tree. Optional relay selection, recipient subsets, resource capacities
and work-dependent output sizes require another formulation or an outer search.

Useful comparisons include shortest paths, direct fanout, fixed-depth or bounded
fanout heuristics, a small frontier of cost/arrival compromises, and local subtree
replacement scored by the simulator. Keep a feasible low-latency incumbent while
searching for cheaper variants. The classic light/short tree result illustrates
the distinct objectives, but its approximation guarantee concerns undirected
weighted graphs; it does not apply to this directed, loaded, transforming graph.
[Khuller, Raghavachari and Young, *Balancing Minimum Spanning and Shortest Path Trees*](https://arxiv.org/abs/cs/0205045).

## Multiple origins and the synthetic root

An origin has a **release time**, a physical sender and the particular bytes and
evidence it can legally send. Being a witness does not make it ready at time zero.
For one already identified message, suppose the fast durable follower F is ready
at 0, the slow follower S at 35 µs, and the leader L after acknowledgement at
55 µs. The following values are an example, not measured network percentiles:

| Destination | F edge | S edge | L edge | Earliest arrival |
| --- | ---: | ---: | ---: | --- |
| C | 12 µs | 4 µs | 1 µs | F at 12 µs |
| D | 80 µs | 5 µs | 8 µs | S at 40 µs |

Use `Ω→F=0`, `Ω→S=35`, `Ω→L=55` in a shortest-path calculation. Equivalently,
collapsing the origins into Ω gives source-labelled outgoing costs `(12,39,56)`
for C and `(80,40,63)` for D. Preserve which physical origin supplies each edge
so that the simulator still charges its CPU, NIC, availability and failure domain.
This collapse assumes independent releases and equivalent usable message copies;
it must not erase a dependency needed to produce the message.

There are two distinct minimum-cost constructions:

- **All origins already possess the message:** force zero-cost `Ω→origin`
  edges and prohibit other incoming edges to each origin. Edmonds then chooses
  a minimum-cost forest. `minimum_cost_forest` implements this construction.
  Release times remain separate when assessing its latency.
- **Charge origin activation once:** ordinary synthetic root edges can carry an
  activation cost. Choosing some origins rather than others can be economical.
  Such an edge cost still does not represent every descendant's arrival time.

Adding a 35 µs readiness offset to every outgoing edge of S charges 140 units
when four children are chosen. The actual release occurs once, and its effect
on completion depends on paths and shared-resource scheduling. The collapsed
weights are valid for the stated distance calculation, but feeding them into
Edmonds changes a sum-of-weights objective without making it a latency objective.
In the worked example the cheapest available-origin forest selects L→C and
S→D, giving C at 56 µs rather than 12 µs.

The shortest-path helper allows a nominally late origin to receive usable bytes
from an earlier one. Disable such incoming paths when receipt cannot substitute
for independently producing required evidence. Actual follower release times
are jointly distributed outcomes of persistence and networking; choosing a
different origin after observing readiness must use information that node can
actually observe, with its notification delay.

## Work and resource costs need more than one edge scalar

Model separate resources for sending, receiving, serialization, link/NIC queues,
encryption, copying, relay compute and retained message storage. Shared-AZ or
public-internet capacity is a shared resource, not an independent capacity on
each logical edge. A 100 ns local ring handoff is a useful authored extreme,
but still consumes producer/consumer core time and cache/coherence bandwidth.
A stalled polling consumer or oversized copy can dominate that edge.

For payload `B`, packet payload limit `M` and per-packet overhead `H`, a first
model has `packets=ceil(B/M)` and `wire_bytes=B+H×packets`. Account independently
for fixed per-packet work, per-byte work and resource occupancy. Real framing,
fragmentation, offload and segmentation can change this relationship. Give
public edges their own price and capacity; do not assume a permanent provider
price or multiply a free local byte by the same coefficient.

One candidate score is money + weighted CPU + weighted bytes + retained state +
repair work, subject to effect-latency and delivery constraints under the tested
workload. Coefficients encode a comparison, not universal exchange rates. When
capacity is nearly exhausted, marginal queue delay is nonlinear. An iterative
planner can price congested resources, regenerate candidates and re-evaluate
them, but must not equate the resulting scalar with a measured tail.

## Many-to-many includes all the degenerate cases

Cardinality counts independent logical origins and actual required receivers,
not redundant physical copies of the same message. Distinguish an item that
**all** receivers require from work that **any one** eligible replica may perform.

| Shape | Candidate pattern | Failure or cost that deserves its own case |
| --- | --- | --- |
| One→one | Direct path, local ring, a route through a useful compute relay | Extra hop and transform must repay themselves; destination death may require a new physical owner of the same logical obligation |
| One→few | Direct fanout or a shallow subtree; recipient-specific branches | Shared sender queue, one slow branch, unequal payload requirements |
| One→many | Regional entry points, local fanout, chunk striping among forwarders | Source NIC, tree depth, last-recipient tail, joining recipients |
| Few→one | Weighted sender election for equivalent results; in-tree for distinct parts | Equivalent copies versus mandatory contributions must not be confused |
| Few→few | Small bipartite assignment or matching, with local co-location preference | Matching can overload a physical host shared by several logical actors |
| Few→many | Released-origin forest; split chunks or receiver groups across origins | Readiness evidence, origins in one failure domain, inconsistent sender choices |
| Many→one | Batch and coalesce frontiers; hierarchical reduction of query parts | Packet processing, incast, relay memory and stalled contributor |
| Many→few | Aggregate by destination authority then assign forwarders; partitioned reducers | A hot key/reducer and uneven result sizes defeat equal-count balancing |
| Many→many | Host-pair multiplexing, local exchange, partitioned shuffle and shared multicast trunks | Per-shard trees hide shared NIC/core pressure; global all-to-all can explode metadata |

The same VM may host producers, consumers and witnesses for several shards.
Compute the physical traffic matrix after resolving actor placement. A local
actor edge must not become a network packet merely because the actors belong to
different shards. Conversely, collapsing roles onto one VM cannot provide two
independent durable copies or two independent failure paths.

## In-arborescences, batches and frontiers

Reversing directed edges turns a fixed-sink in-arborescence into an ordinary
out-arborescence problem. It does **not** solve aggregation scheduling: outgoing
size and send time depend on which inputs have arrived and what can combine.
An internal compute node should carry that transformation and its memory demand.

For admission frontiers, componentwise maximum of **already established
contiguous persisted prefixes** is associative, commutative and idempotent.
`merge_frontiers` checks only the integer representation; callers establish
the evidence. Receiving raw LSNs 8 and 10 does not prove contiguous receipt
through 10. Producer incarnation and stream identity must prevent a restarted
stream from being merged with a different one. Two signatures from the same
failure domain do not become two independent durable copies through aggregation.

For “all designated consumers have accepted through this point,” minimum across
the designated consumers is relevant instead. A quorum frontier needs the
required order statistic over distinct eligible authorities and their evidence.
Neither follows from the admission-frontier maximum. A frontier that summarizes
delivery also cannot establish application execution or external effects.

For packet economics, author 256 independent 24-byte frontier updates, a 1,200-byte
packet payload limit and 96 bytes of per-packet overhead. Direct delivery costs
256 packets and 30,720 wire bytes. Packing them costs six packets and 6,720 wire
bytes **on the relay→witness edge**. It does not delete the 256 incoming packets
or their receive cost. This can protect a shared witness, especially if incoming
updates use local rings or local batching; a remote relay can otherwise add work.
The example is exact arithmetic under these assumptions, not a NIC measurement.

A relay can flush at a byte limit, count limit or deadline, immediately forwarding
a latency-critical first update and batching subsequent progress. It can replace
old unsent prefixes for the same stream with newer certified ones. Measure
freshness lost, per-stream fairness and buffer occupancy, including low traffic
where a fixed batching wait buys nothing. Never wait for every child to advance
unless the downstream obligation requires every child.

General reductions need an explicit algebra: sums are duplicate-sensitive;
floating-point reduction order can affect results; exact distinct sets may grow;
averages require sum and count; top-k needs the applicable tie and partition rules.
Partial aggregates may contain contributor IDs or completion ranges so duplicate
repair does not double count. TAG provides a useful taxonomy of duplicate
sensitivity and partial-state size, while SwitchML supplies a concrete example
of reducing network volume with programmable aggregation. Their hardware,
semantics and failure machinery are not inherited by SixDB.
[TAG, OSDI 2002](https://www.usenix.org/legacy/event/osdi02/tech/full_papers/madden/madden.pdf),
[SwitchML, NSDI 2021](https://www.usenix.org/conference/nsdi21/presentation/sapio).

## A relay can enhance, split or delay a message

Message enhancement includes exact filter selections, computed columns,
partition/routing information, partial aggregates, shared dictionary references
and execution plans. Some enhancements add bytes while avoiding repeated work;
others let downstream stages omit inputs or intermediates altogether. Treat an
enhancement as a derived artifact tied to input identity, relevant snapshot or
admitted history, executable version and the scope of its correctness claim. A relay
cannot decide extra authority merely because it is early. If local timing changes
the plan, every permitted plan must preserve the same agreed semantics.

For example, an exact selective-filter bitmap can avoid re-evaluating a predicate
on every replica. Sparse row IDs, dense bitmaps and runs have different costs.
A physical row mask requires a compatible row mapping; a logical-ID selection
may transfer between representations at an extra lookup cost. An approximate
filter can prune only under its stated error contract, not masquerade as an
exact result. [The enhancement study](ENHANCEMENT.md) compares this case separately.

The plan-sharing fixture compares these compositions for independently arriving event bytes and admission:

1. Forward both immediately; downstream consumers join and plan locally.
2. Join near a witness, spend compute once, append a reusable plan, then fan out.
3. Forward evidence immediately; send the optional plan separately so it can
   overlap data transfer and does not hold the required branch.
4. Produce a compact plan reference, letting only interested consumers fetch it.
5. Send canonical inputs across expensive links; compute or expand the plan on
   the other side. Send larger enhanced messages only along cheap local edges.

A shared planner's extra critical-path cost is at least the uncovered input-join
wait plus its queued compute time, and may also include extra serialization.
Its benefit is avoided downstream compute and perhaps smaller downstream work.
No scalar benefit threshold is valid without the number of consumers and the
price/capacity of each affected edge. Make plan size, compute size and cache hit
rate independent sweep dimensions: many cheap 100-byte transforms and one
expensive 100-KiB dependency plan are different workloads.

Chunking may permit prefix planning and forwarding, but an epoch plan requiring
complete conflict information cannot safely certify independence from a partial
epoch unless its producer supplies a conservative bound. A late plan can remain
a hint whose rejection costs only redundant compute. The owning module decides
whether such a hint is useful and how to validate it.

## Sender choice, tree families and adaptive hierarchy

For equivalent copies, rank eligible senders with a stable hash of message ID,
candidate ID and an agreed salt. An entropy reservoir is unnecessary for this
particular deterministic selection: every sender can derive the same ranking
without communicating. The supplied weighted exponential race gives larger
capacity weights larger winning shares. Removing a candidate preserves the
relative order of survivors; a candidate-set epoch need not be hashed into every
score, which would churn all choices. This is a routing policy, not consensus or
permission to execute an effect.
[Thaler and Ravishankar, *Using Name-Based Mappings to Increase Hit Rates*](https://www.cs.kent.edu/~javed/DL/papers/web/p1-thaler.pdf)
is the primary HRW mapping precedent; the weighted score here is a local variant.

Candidate membership, eligibility, weights and message identity must agree for
identical choices. Local health observations may disagree and activate several
backups. A ranked fallback needs an observable progress rule, bounded timeouts
and duplicate-safe receiving. The ranking alone supplies none of these. If one
actual sender cannot know that the higher-ranked copy even exists, waiting for
it can lose the latency benefit of independent origins.

`tree_family` starts with the true minimum-cost tree, perturbs costs with a fixed
seed and penalizes previously used edges. It returns at most the requested
number of distinct candidates. Perturbations explore alternatives; they do not
prove load balance, failure independence or latency. Several edge-disjoint
logical trees can still share the same host, NIC, AZ uplink or witness.

More useful diversification dimensions are physical first hop, NIC queue,
failure domain, regional gateway and critical consumer path. Reject a candidate
that misses an obligation or violates its path budget before considering its
savings. Weighted tree selection can divide message IDs or chunks across a small
family; select per message when reassembly/reordering is costly. Selecting whole
large flows preserves locality but can leave a single hot flow unbalanced.

A hierarchical route can name a destination region or subnet and a small eligible
gateway set, leaving local fanout to that domain. This reduces distributed route
state and the scope of updates. Charge the loss of detailed global optimization,
gateway overload and local failover delay. An edge naming “region B” must still
resolve to concrete receivers for delivery accounting, especially during joins.

Adapt on measured windows with a held-out check, minimum useful gain and switching
cost. Freeze a good assignment long enough to distinguish measurement noise
from change. Moving a route, moving work, redirecting a client, changing leader
and relocating witnesses are separate interventions. A network solver can price
copying history and interrupted service; it cannot perform witness authority
transfer or choose a new agreed state. Several shards adapting to the same
apparently idle relay can herd into overload, so charge shared budgets or limit
simultaneous migrations.

## Read scale-out and extension rings

Read requests should carry the same coherent snapshot/cut and necessary identity
through the work graph. Per-shard freshest replicas do not automatically compose
into a coherent cross-shard read; [the read study](../orbital-scenarios/reconsideration/READS.md)
owns the semantic examples. Eligibility also depends on retained versions, data
representation, required executable, capacity and any verification policy.

Within one shard, partition scan ranges, index probes or independent groups
across replicas using stable work IDs. Return each contribution once to its
reducer; “every replica repeats the whole read” is verification or redundant
execution, not the same scale-out plan. Account for overlapping input reads,
warm caches, duplicated planning, result merging and skew. Borrowing an idle
replica may accelerate a read but delay its fold/catch-up, making it ineligible
for the next read.

Across shards, compare coordinator gather, local partial reductions, directed
shuffle by partition and direct final-result delivery. Early filtering, predicate
pushdown, semi-join key exchange and co-located producer/consumer shards can
remove large transfers. Preserve exact query semantics and charge the additional
compute, sketches or intermediate state. For skew, dynamically split an
unstarted range or hot reducer partition where the operator allows it; avoid
turning physical work stealing into duplicate logical contributions.

In a ring, a client may submit to A, A forwards to data owner B, and consumer C
returns the response or starts the next backend stage. Correlation, authentication,
cancel propagation and reply destination survive the path. Acknowledging A's
receipt is not completing C's effect. Some extension pipelines may never return
to A; the initiating client must not be invented as the completion sink.
Inter-shard extension traffic can therefore be both dissemination and a next
stage's admission input. Charge any persistence/admission that stage requires.

Hedged reads can reduce isolated stragglers when spare capacity exists, but
duplicate work can worsen overload. Try a capped hedge budget, delayed launch,
cancel-aware work units and separate mandatory native verification. Any hedge
must use the same read cut and preserve result identity. The *Tail at Scale*
paper motivates measuring fanout and redundant-work tradeoffs; it does not
establish the requested SixDB microsecond targets.
[Dean and Barroso, 2013](https://research.google/pubs/the-tail-at-scale/).

## Delivery when a tree or its membership becomes obsolete

A tree is a forwarding plan, not a durable delivery obligation. Keep logical
message identity, required recipient or destination group, retention source,
acceptance frontier and route generation separate. A forwarding relay can die
after acknowledging receipt; a surviving retained source must be able to repair
downstream gaps if the obligation requires delivery. Charge receipt reports,
inventory exchange, repair bytes and retained state.

Targeted repair from a nearby holder, frontier comparisons, redundant critical
paths and proactive parity are candidates with different costs. NAK suppression
and aggregated receipts can reduce incast, but silent loss of the final message
requires a heartbeat/frontier/end marker or another way to discover the gap.
PGM is a useful precedent for local repair and NAK suppression. Its documented
guarantee allows detection of unrecoverable loss within a transmit window and
does not establish acknowledged delivery to a known recipient group; it cannot
be substituted for Orbital's obligation tracking.
[RFC 3208, §§1.1–1.2](https://www.rfc-editor.org/rfc/rfc3208.html).

When a recipient is added during propagation, distinguish:

- A fixed message recipient set: the new VM receives its required historical
  prefix through snapshot/catch-up, then joins live delivery at a named boundary.
- A live group obligation: membership adds outstanding deliveries explicitly;
  retained holders repair them even if the old tree never mentioned the new VM.

Both can use an overlap interval: install the new receiver's catch-up boundary,
receive old and new routes, deduplicate, confirm contiguous acceptance, then
retire old forwarding state. Control messages can reorder or be lost too.
Holding an old tree forever is not a substitute for retention and versioning;
dropping it immediately creates gaps. New receiver activation requires a defined
history obligation, not the impossible promise to receive every message ever
sent before its existence with no retained history.

Retries cannot repair destroyed unique content, create unavailable authority,
prove absence of an external effect or bypass a permanent partition. If an
effect target dies after acting but before confirming, replay requires an
application-supported idempotency/transaction mechanism or an explicit uncertain
outcome. A different VM receiving the same bytes is useful only when the logical
obligation allows it to take over. Report unrecoverable, pending, refused and
duplicate work separately from successfully completed latency.

## Executable boundary and retained comparisons

Run the checks with the repository's Linux environment:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_routing.py
```

The retained check covers 6,384 exact small-graph cases against an independent
parent-choice/reachability oracle: all three-node edge weights in `{-1,0,1}` for
every root, all four-node directed topologies, random graphs through six nodes
including parallel edges and a singleton. Another 100 cases compare released
shortest paths with repeated relaxation. It also checks the authored chain,
readiness and reversed-in-tree examples, deterministic tree families, 10,000
weighted sender rankings/removals, frontier algebra and invalid inputs.

| Helper | Result and limit |
| --- | --- |
| `minimum_arborescence(nodes, edges, root)` | Original `Edge` objects minimizing additive cost; modest graphs and recursive contraction |
| `minimum_cost_forest(nodes, edges, origins)` | All origins forced available; no activation or readiness optimization |
| `shortest_path_forest(nodes, edges, ready_us)` | Earliest unloaded deterministic paths; no shared-resource contention |
| `tree_metrics(nodes, tree, ready_us)` | Cost, structural depth/fanout and arrivals; inspect the actual required recipient rather than all-node maximum |
| `tree_family(..., count, seed, jitter, reuse_penalty)` | At most `count` distinct perturbed plans; no diversity or optimality guarantee for the family |
| `sender_order(message_id, candidate_weights, salt=...)` | Stable weighted routing preference; no health, possession or authority evidence |
| `merge_frontiers(prefix_maps)` | Componentwise maxima of certified prefixes; no certification or hole discovery |

Next comparisons belong in the event simulator: small versus broad shards,
many shards sharing witnesses, same-host cross-shard rings, public-edge planning,
frontier incast, skewed read partitioning, a stalled relay, loss after receipt,
membership changes in flight, correlated domain loss, and saturating independent
CPU/packet/byte budgets. Candidate rankings must be recomputed at actual offered
load and failure timelines. The requested 170 µs p99 / 250 µs p99.9 effect targets
remain user objectives; none of these static path calculations measures them.
