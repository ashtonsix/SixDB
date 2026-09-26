# Dissemination findings

2026-09-26. The useful design space is wider than a choice of tree algorithm.
Recipient obligations, packet count, shared host capacity, useful computation,
read coverage and recovery traffic change which route is worthwhile. The
comparisons support several concrete directions and expose counterexamples;
they select neither a transport nor a witness topology.

The expanded [catalog](CATALOG.md) retains 223 source-linked entries from current
instructions, older networking designs, notebook/spike work and execution/recovery
archives. Each has payoff conditions, a counterexample or limiting cost, and an
explicit evidence boundary. [Design space](DESIGN-SPACE.md) generalizes them into
work/representation placement, release conditions, obligations, retention and
partial observations. The experiments below are selected probes of that wider
space; analytical exploration is not confused with measurement.

All numerical results below are **synthetic**, using the parameters and limits in
[MODEL.md](MODEL.md). They are not measured SixDB performance. The retained
[comparison](evidence/20260926/comparison.json) has 95 cases: 76 write/message
cases with 1,000 offered operations each, 14 read cases with 100 queries, and
five join cases with 80 offers. Source hashes and complete selected configurations
are included. Small deterministic cases expose dependencies and resource pressure;
they do not establish a p99.9 confidence interval or an SLA.
The additional campaigns and their exact source hashes are listed in the
[evidence guide](evidence/20260926/README.md).

## Origin placement changes the network bill before tree selection

The old edge-origin idea was initially missed. [Edge economics](EDGE.md) now
separately explores regional trunks, cheap-provider/edge relays, small direct
hash exchanges, request/reply direction, private-link fixed costs, peer/blob
request costs and avoiding metered NAT processing. These are distinct choices.

For 100 synthetic 16KiB broadcasts to eight consumers, cloud-direct transfer
cost is 702.400 millionths of an illustrative unit, regional trunks 351.200,
cloud-to-edge relay 181.176, and edge-origin regional fanout 32.224. Cloud-to-edge
relay adds roughly 10ms; starting at the edge avoids that first cloud-egress leg.
The same edge-origin fanout with 64KiB replies costs 5,040.352 millionths: result
bytes now dominate in the other direction. All those healthy cases complete.
With only 256KiB reply buffers, synchronized fan-in fails despite modest average
NIC demand. Costs are authored, not real provider quotes or deployment advice.

## Route portfolios and mixed workloads need useful work granularity

The [34-case route/sender comparison](ADAPTATION.md) adds a constrained shallow
family to the unbounded-noise baseline. At 4KiB and 100,000/s, direct and fixed
cheapest routes complete only 4 and 10 of 600 fanouts; shallow interleaving
completes all with 133.10µs p99. At 32B with expensive packet processing, choosing
the same shallow routes in blocks of 32 produces 15.95ms p99 and 6,445 retries,
versus 180.55µs and no late messages for per-message interleaving. Equal long-run
shares do not ensure useful short-window balance. The family install traffic is
charged; this is not a demonstrated live route-rewrite controller.

In the [mixed study](MIXED.md), splitting 128KiB extension jobs into 8KiB chunks
changes foreground proxy p99 from 853.69 to 152.12µs while background read p99
rises from 849 to 1,326µs. Two separate core pools preserve the foreground
baseline but slow the background further. Reserved queue credits do not preempt
an active job. A 16× output expansion completes only 3 of 30 reads on the shared
host while foreground latency looks healthy. Cancellation also needs a real
stopping point; it does not immediately release a large running job's resources.
This foreground is a durable-notification proxy without witness admission,
**not** a measurement against the user's durable-effect target.

## Packet batching and frontier reduction solve different costs

Thirty-two producers feed one relay and receiver, with 32-byte records arriving
in bursts of 16. At 500,000 total offers/s and one modeled packet-processing core:

| Relay policy | Completed / offered | p99 of completions | Offered fraction within 250 µs | Packets |
| --- | ---: | ---: | ---: | ---: |
| Forward individually | 816 / 1,000 | 2,182.74 µs | 6.8% | 5,698 |
| Pack up to 1,200 bytes, wait at most 4 µs | 1,000 / 1,000 | 1,161.18 µs | 16.7% | 2,909 |
| Full frontier map, no packet batching | 986 / 1,000 | 2,069.25 µs | 19.0% | 4,414 |
| Full frontier map and packet batching | 1,000 / 1,000 | 1,898.70 µs | 21.3% | 3,847 |

Batching cuts packet processing and repair amplification. It does not make this
offered load meet a tight deadline. Frontier aggregation improves one completion
fraction while worsening p99 relative to ordinary batching. The fixture transmits
a 32-stream map even when few streams changed: compact semantic metadata can be
larger than the updates it replaces. At 100,000/s, ordinary batching takes
61.40 µs p99 and 376,000 wire bytes, versus 69.36 µs and 581,208 bytes with the
full frontier map. Dirty-stream maps, delta frontiers, periodic complete progress
and deadline-sensitive flushes are distinct candidates to compare.

Aggregation must preserve the algebra. Certified contiguous producer progress
uses per-stream maximum; an all-consumer release frontier uses minimum over
the required set; read reduction requires exact contributor coverage. An
in-arborescence does not make these interchangeable. [Delivery](DELIVERY.md)
retains the gap and duplicate histories.

## Cheap routes, short paths and balanced work are different objectives

For two origins and sixteen receivers all in another AZ, 256-byte messages at
20,000/s give:

| Routing baseline | p99 | Relative illustrative egress charge | Total wire bytes |
| --- | ---: | ---: | ---: |
| Direct or shortest paths | 105.62 µs | 16 | 7,552,000 |
| Minimum-cost arborescence | 114.03 µs | 1 | 7,552,000 |
| Unconstrained perturbed family | 201.71 µs | 1 | 7,552,000 |

One cross-AZ trunk plus local fanout reduces expensive crossings while adding a
hop. Noise alone produces deeper trees without further egress saving. Edmonds
is useful as a cost baseline and candidate generator; it does not optimize
critical-path length, tail latency or shared-resource service. Source release
times belong in the completion DAG, not merely added to each outgoing edge's
scalar cost. [Algorithms](ALGORITHMS.md) has exact counterexamples and the
synthetic-root constructions.

The target recipient and background recipients also differ. In the 24-consumer,
100,000-write/s fixture, direct and chain routes give almost identical target
p99s, 159.59 and 160.16 µs, because the target is first in the chain. All-consumer
p99 changes from 221.80 to 318.18 µs. A four-way tree gives 229.61 µs for all
consumers. Optimizing one useful endpoint can leave substantial replication lag.

Sending both follower branches to every recipient costs more than redundant
network bytes: under the same 64-KiB queue limits, the high-rate redundant case
finishes only 246/1,000 target effects and 166/1,000 complete fanouts. It refuses
258 admissions and leaves 754 target effects unfinished. Its completed p99 is
2,572.92 µs. Redundancy can protect a particular failure path, but blanket
duplication under pressure is not a general reliability improvement.

## Read scale-out has a useful grain; hedging spends capacity

Four shards each scan 1 MB per query, with four replicas per shard. The same
100 queries read 400 MB in every unhedged case:

| Pieces per shard | p99 with separate shard workers | Wire bytes |
| --- | ---: | ---: |
| 1 | 1,235.52 µs | 1,998,400 |
| 4 | 485.66 µs | 2,497,600 |
| 16 | 481.12 µs | 4,878,400 |

Four pieces expose most of the available parallelism. Sixteen pieces buy only
4.54 µs in this fixture while nearly doubling wire traffic relative to four.
This is a workload comparison, not a preferred global chunk count.

Immediate-enough hedges at four pieces scan 800 MB with the same 485.66 µs p99.
When those four shards share physical workers, the unhedged four-piece case
finishes all 100 queries at 1,225.39 µs p99; hedging finishes 77 by the drain with
33,724.91 µs completed p99 and 530 MB of admitted scan work. The rest remains
unfinished or refused by finite scan queues. A hedge policy needs spare capacity,
useful straggler evidence and cancellation granularity; a timer alone can make
the tail worse.

With one replica per shard 600 µs behind the chosen cut, the round-robin case
takes 965.22 µs p99. The optimistic reservation/readiness-aware selector takes
724.91 µs. That selector knows instantaneous readiness; the control traffic and
staleness of obtaining it are not established. Both keep one cut and exact chunk
coverage. Splitting query serving does not split mandatory state-fold or native
verification work. [Reads and extensions](READS-AND-EXTENSIONS.md) develops the
broader operator, skew, dynamic discovery, result and extension-ring cases.

## Message enhancement changes both downstream work and representation

Enhancement includes selection masks/row IDs, computed values, partition maps,
partial aggregates and plans. It can enlarge a message to save repeated work, or
let receivers avoid transmitting or processing irrelevant data. Input residency,
row-domain compatibility and selectivity change that tradeoff. The
[selective-filter study](ENHANCEMENT.md) complements the plan-like metadata
fixture below; these numbers do not characterize all message enhancement.

The 40 exact-selection comparisons make the residency distinction concrete.
With bases already resident and 10% matches, one relay evaluation cuts predicate
service from 4,915.2 to 1,638.4µs but increases wire bytes from 6,720 to 22,400 and
slightly worsens completion. With no bases resident and 0.1% matches, shipping
the selection **and qualifying rows** cuts wire bytes from 3.34MB to 13.57KB.
Optional sidecars can add computation even when the ordinary path already won.
Empty results still require bound, complete coverage; absence is not emptiness.

For eight local consumers, planning once near each follower and sending a
512-byte hint changes target p99 from 158.85 to 161.02 µs. Sending the hint as an
optional sidecar gives 159.07 µs and more traffic. Saving repeated computation
does not automatically improve the critical recipient's arrival time.

Large hints over a constrained public edge are more consequential. With a
4-KiB hint, the blocking case sends 81.37 MB against 13.82 MB without hints. The
near-follower target stays fast, but only 32/1,000 all-consumer obligations finish,
versus 641 without hints. The smaller completed all-consumer p99 in the hinted
case is not an improvement: it describes a much smaller successful subset.
These cases also use a 400 µs retry timer shorter than the authored public RTT;
the [relay sensitivity](evidence/20260926/relays.json) separates that bad timer
from the larger message's capacity demand.

With a 3,000 µs timer on the same public fixture, the no-hint case finishes all
1,000 fanouts at 1,276.00 µs p99, sends 8.40 MB and performs **zero retries**,
compared with 16,000 retries under the 400 µs timer. The large blocking hint still
finishes only 71 fanouts and sends 78.31 MB. Fixing the observation timer removes
self-inflicted duplicate traffic; it cannot remove the extra bytes. Both controls
matter, and a longer timer carries a different failure-reaction cost.

This supports comparing bounded input joins, optional hints, compact references,
planning after an expensive crossing, and sending enhanced representations only
where recipients benefit. The hint's version, input binding and correctness
status remain explicit. Neither early arrival nor matching final bytes grants
the authority to skip an extension's required checks.

## Delivery is an obligation that survives the route

The network-backed join cases keep the old `p→c` route unchanged. A newly
subscribed `j` receives an atomically registered snapshot plus pinned tail through
bounded repair windows. Healthy, temporary-partition and process-crash cases each
eventually deliver all 80 accepted events and complete the join during drain.
This is charged network/device work, not an oracle declaring the new VM current.

With sixteen retention credits, the selected join case accepts 59/80 offers and
refuses 21 while protecting owed history. When `j→p` application receipts remain
lost, it accepts 31, refuses 49, retains sixteen obligations and never reports
the join ready. Receiver persistence and source knowledge of it are different.
Completed persistent receiver state survives the modeled process crash by
assumption; cross-domain destruction requires another recovery model.

The ledger probes additionally show that stable IDs with lost completion metadata
can repeat an effect; recording an external effect before acting can omit it,
while recording after acting can repeat it. Retries, a chosen sender or a new
VM do not resolve this ambiguity. Logical recipient identity, incarnation,
effect completion and release/retention evidence have different lifetimes.

## Concurrency is promising, but early ordering can spread a wait

The point-write overlap experiment is one part of this study. It does not prescribe
a topology. Under the favorable 100 µs cross-AZ, 8 µs same-AZ, 10 µs persistence
service fixture:

| Boundary | Eligibility-before-proposal baseline | Conditional early-proposal candidate |
| --- | ---: | ---: |
| Two-domain payload readiness p99 | 121.25 µs | 121.38 µs |
| Near-follower admitted internal effect p99 | 371.71 µs | 158.85 µs |
| Client return p99 | 472.55 µs | 259.69 µs |

The candidate overlaps payload transfer with proposal/journal work. With constant
service, p99.9 equals p99 here. It clears the authored effect target, but changing
same-AZ propagation from 8 to 20 µs raises effect p99 to 182.83 µs; at 80 µs it
becomes 302.83 µs. Three synthetic jitter/loss seeds give candidate p99.9 values
of 845.15, 577.01 and 569.77 µs. None of this validates a real 170/250 µs target.

More seriously, early slot allocation can turn one missing payload into a shared
prefix wait. The separate [producer-attributed comparison](evidence/20260926/prefix.json)
offers 100 operations from two producers sharing a shard, blocks producer 0's
remote payload paths, and leaves producer 1's paths healthy. Bounded transport
retries are enabled; all operations eventually finish:

| Fault lasts | Baseline: healthy producer p99 | Candidate: healthy producer p99 | Healthy producer effects before fault ends: baseline / candidate |
| --- | ---: | ---: | ---: |
| 300 µs | 416.33 µs | 532.20 µs | 0 / 0 |
| 600 µs | 843.60 µs | 1,331.75 µs | 11 / 0 |

For the 300 µs incident, aggregate p99 improves **777.48→541.43 µs** while the
healthy producer gets slower. The baseline cannot itself finish before that
300 µs fault ends; its normal dependency path is already longer. The 600 µs case
shows independent progress during the outage and the candidate's roughly
1,171 µs maximum admitted-prefix wait, including repair timing after the link
returns. Both policies issue 54 transport retries in that case. The test with
retries disabled retains indefinite obstruction as a separate policy-specific
counterexample.

Every choosing quorum, recovered proposal and handoff still needs the required
payload durability evidence. Other placements, integrated payload-bearing
journals, combined persistence or delaying order allocation may offer different
overlap. They need their own recovery and locality argument. Healthy timing alone
does not establish that the conditional early-entry mechanism earns its cost.
The current brief's eligibility-before-proposal rule remains the reference.

The [26 adaptive-timing cases](PROPOSAL-TIMING.md) preserve that distinction.
A local watchdog changes future early/strict choices using real durable replies
and timeouts. During a sudden payload partition, it leaves independent-producer
p99 near 1,332µs, versus 848µs for always strict: the existing hole survives the
switch. A sufficiently early warning improves that producer's later service,
while an expired warning makes it worse than both fixed policies. This supports
considering observation horizons and per-producer outcomes; it does not select
adaptive early proposals or a global infrastructure-health bit.

## Packet and redundancy choices depend on the loss regime

The [transport comparison](TRANSPORT.md) has 24 finite-queue cases and 44 separate
ideal-code calculations. Increasing MTU rescues one 64KiB packet-CPU overload but
does nothing for 64-byte messages already fitting one packet. Losing reverse ACKs
leaves first-delivery latency unchanged while raising wire traffic from 27.87 to
47.80MB. Ideal 8+2 FEC helps independent erasures, gives no recovery advantage for
a whole-block cut, and pushes an 85%-utilized zero-loss stream to 106.25% offered
load. These are arguments for jointly charging packet work, repair granularity,
correlation and capacity—not a choice of jumbo MTU or production FEC.

## What is ready to carry forward

[The provisional recommendation](RECOMMENDATION.md) separates the composed
direction, choices supported strongly enough to carry forward, rejected defaults
and material gaps. Orbital LEAD selects any brief promotion.

The shared simulator, static planners, read coverage, delivery ledgers and dynamic
join fixtures are usable research tools with bounded checks. Their strongest
design lessons are to charge physical host resources, distinguish semantic
reduction from packet packing, select work grain with output costs, preserve
logical obligations across routes, and judge independent traffic as well as
aggregate completion. None requires adopting Edmonds or the example placement.

Continuous adaptation under stale observations, client redirection, physical
witness relocation, mixed skewed operator graphs, selective/FEC repair and
stateful reductions across logical range movement remain open comparisons.
The current code does not claim a production congestion controller, membership
consensus or calibrated cloud tails. Those limits are part of the handoff to
Orbital LEAD, along with the executable counterexamples and useful alternatives.
