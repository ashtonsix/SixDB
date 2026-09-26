# The general problem behind the examples

The investigation is about **where to do work, what representation to transfer,
which obligations must complete, and when to adapt**, under shared resources,
incomplete information and failures. Ashton's examples expose dimensions of that
problem. They do not prescribe trees, particular follower roles, entropy
reservoirs, one enhancement type, or a fixed deployment.

This is an organizing model for research, not a proposed universal API or a
request to generalize the prototype into the deferred all-Orbital simulator.
The concrete studies test parts of it and reveal where those parts interact.

## Start from evolving work and obligations

A useful model has several connected descriptions. A static network graph alone
cannot express them all.

| Description | What it contains | Why it matters |
| --- | --- | --- |
| Application work | Inputs, transformations, retained state, outputs, continuations and possible future work | A tiny request can create a scan, large shuffle, mutation and tiny response. A stream or feedback loop is not one fixed message. |
| Semantic obligations | Who needs what result or evidence, at which cut, by which completion event | All replicas, any eligible reader, a quorum, exact range coverage and one external effect are different obligations. |
| Representations | Canonical input, references, compressed data, chunks, selectors, derived values, partial results and evidence | Equivalent usable information can have radically different size, CPU demand and retention dependencies. |
| Release conditions | Available inputs, eligible durable copies, accepted history, coverage, verification and authority | Work can overlap only where its actual dependencies permit it. Readiness can differ across copies and representations. |
| Physical resources | Hosts, cores, memory, NICs, links, storage, failure domains and finite buffers | Many logical edges can share one bottleneck; several roles can become one memory transfer. |
| Policy and observations | Placement, routing, scheduling, batching, repair and adaptation using available evidence | A centralized simulator's true state is not information a real sender or controller can use for free. |

The workflow evolves: operators discover more sources, consumers join, partitions
move, results make speculative work unnecessary, and streams produce more work.
Use an AND/OR or coverage description when a fixed DAG is helpful, but permit
feedback and dynamically created work. The realized causal trace can still
explain one completion or failure afterward.

An obligation can require **all**, **any one**, **k qualifying members**, **one
contribution per logical range**, or an application-defined predicate over
results. These predicates are not generally counts of packets or VMs. A quorum
needs distinct qualifying authorities/domains and the right evidence; a read
needs coverage at its cut; a result already computed still needs delivery or
retention. Negative obligations matter too: do not repeat an effect, do not
release a still-needed artifact, and do not let an obsolete owner act.

This immediately generalizes the sender/receiver cardinalities. Several senders
may hold equivalent copies of one required result, or each may hold a necessary
different contribution. The first permits selecting a sender; the second needs
combination or coverage. Many receivers may all need the same information, need
different projections, or be interchangeable execution choices. The physical
many-to-many matrix appears only after resolving those meanings and placements.

Choose **who derives, how many executions are required, who sends, which evidence
is required, and how long each representation must remain available** separately.
One execution per locality, mandatory independent verification, one selected
sender and many observers can coexist. Neither replica count nor a route tree
determines all of those numbers. Representation equivalence is relative to the
obligation: a projection can satisfy this query while being insufficient for
replay, auditing or another subscriber. The necessary canonical input can remain
at designated holders rather than accompany every projection everywhere.

## Representations and transformations belong in route selection

A communication step can copy, batch, split, filter, transform, join, aggregate,
compress, verify, persist or execute. The application supplies its semantics and
validity rules; Orbital does not infer a predicate or reduction from a route.
A relay's extra work can shrink, preserve or expand the outgoing representation.

Examples form a larger family than plan hints:

- A query description can become an exact row selection, projected values or
  a materialized result. Sparse IDs, dense masks and runs are alternatives.
- Canonical events can become computed expressions, feature vectors, locally
  derived indexes or dependency/parallelism information.
- Many contributions can become a partial aggregate, partitioned exchange,
  progress certificate or compact set of missing ranges.
- Large repeated data can become a reference, shared dictionary ID, delta or
  immutable intermediate reused across several messages or queries.
- Work can become a continuation sent to resident state, replacing a data
  transfer; an executable and its captured context also have size and lifetimes.

Each representation has conditions for use: input/content identity, cut or
history, operation/version, row-domain mapping, completeness, verification and
required resident dependencies. An empty exact result must carry completed
coverage; silence is not that result. A reference saves bytes only if its receiver
already has the referent or pays to fetch it. Physical row masks may not transfer
between replicas with different layouts; logical IDs trade that compatibility
for lookup and encoding work.

The economic comparison is the **complete remaining work**, not whether the
message got smaller. Compare repeated receiver computation and raw transfer with
shared computation, enhancement transfer, receiver validation/use, materialization,
retention and repair. Include warm and cold cases. A larger message may be cheaper
overall; a smaller message may trigger expensive random fetches. Reuse across
messages can repay preparation that reuse within one message cannot.

Latency needs the same accounting along the actual dependency path. Shared
computation starts after its required inputs are available, waits for resources,
and produces a representation that then travels. Receivers may proceed with a
base representation while an optional enhancement arrives later. Optionality
removes a semantic wait but does not remove CPU, NIC or buffer interference.
Where an operation can safely use a prefix, streaming can overlap its stages;
where complete coverage is required, a partial result cannot certify the whole.

Routing and transformation should therefore be considered together. A cheap
cross-region gateway may be a poor place to expand a message, and a slightly
longer route may be worthwhile if it performs a high-value reduction before an
expensive edge. Out-arborescences, in-arborescences and bipartite assignments are
useful special cases of this placement and scheduling problem. Multicast trunks,
chunk distribution, meshes, operator exchanges and cyclic continuation networks
are other possibilities.

## Concurrency follows release conditions, not role names

The payload-follower example asks whether independent work can share elapsed
time. Generalize it to a graph of **facts that release work**:

1. What inputs or evidence does a step truly require?
2. Which can be produced independently, streamed incrementally or prepared early?
3. Which intermediate results may safely travel before becoming authoritative?
4. Which commitment would remove a useful later choice or create a shared wait?
5. What survives a failure at every partial-completion boundary?

Candidates include overlapping transfer with durable writes, preparing state or
code before demand, forwarding received chunks while the remainder arrives,
computing on immutable inputs before admission, separating a result from its
authority evidence, pipelining analysis and materialization, and sharing a write
or transfer that satisfies several obligations. Combined records and co-located
roles are possible implementations of overlap, not its definition.

An early decision can also create a new dependency. Fixing an ineligible item in
a shared ordered prefix turns a local input delay into a wait for unrelated ready
work. Starting too many partially joined tasks can exhaust memory. Holding output
credits until a slow client drains can stop upstream operators. These are the
same broad problem: a choice intended to expose concurrency retains or orders
resources in a way that couples otherwise independent work.

An acyclic application dependency graph does not prevent resource deadlock.
Two operators can each hold input credits and wait for output space held by the
other. Reserve an escape path, acquire an appropriately bounded input/output
budget, spill, or provide a safe stopping/restart point according to the workload.
Each option spends memory, I/O or repeated work; a control-priority queue alone
cannot release a nonpreemptible job's active reservation.

Multiple origins are instances of differing release conditions. Each usable copy
has its own readiness, representation, evidence and physical resource demands.
A synthetic source is one calculation technique for a fixed version of that
problem; it must not erase which actual host sends or when a copy becomes usable.
Readiness and failures can be correlated. Selecting a later copy after observing
it become ready requires that observation to reach the decision-maker.

## Optimize complete service and amplification together

The objective is a set of tradeoffs, not one universal edge weight:

| Outcome | Useful accounting |
| --- | --- |
| Latency | Typed endpoint, original arrival time, completion distribution and deadline fraction over all offered work |
| Throughput | Offered, admitted, successful, refused, late and unfinished rates, including drain/backlog behavior |
| Work amplification | Actual repeated CPU, verification, scans, copies, persistence and repair versus the workload's declared useful work |
| Network cost | Packets as well as bytes, by link/price class; setup, ACKs, retransmission, coding and control traffic |
| Resource pressure | Shared service demand, queue age, finite retained bytes, nonpreemptible job length and outstanding credits |
| Reliability | Required obligations completed, still owed or explicitly released; recovery dependencies, uncertainty and destroyed information |
| Adaptation | Detection lag, false reactions, control distribution, state movement, transition overlap and instability |
| Interference | Effects on independent producers, shards, tenants and traffic classes, not just the aggregate winner |

There is no meaningful single amplification ratio without specifying the useful
denominator. Mandatory verification is not avoidable duplicate computation; a
query expansion that produces required output is not wasted traffic. Record
both the logical obligation and actual resource work so comparisons can preserve
semantics while exposing unnecessary amplification.

Screen candidate placements against offered/admitted resource demand before
ranking tails: summed demand must fit usable capacity with headroom. This is
necessary, not sufficient under bursts, correlated stalls and finite buffers.
For retained objects of size B with residence T, average retained occupancy
under a stationary arrival model is λ·E[B·T], not generally λ·E[B]·E[T]. Large
results may also drain more slowly. For a changing representation, integrate its
actual byte occupancy over time. Shared-query cancellation needs reference-aware
retirement; one caller leaving does not release another caller's result.

Economic edges also need more than one fixed scalar. Origin location determines
whether the initial expensive transfer exists at all; responses can reverse the
dominant byte direction. Fixed ports, request charges, volume tiers, provider
credits and shared facilities couple workloads and time horizons. Separate
marginal routing cost from facility/placement choices, including cold state,
interpretation data, setup, trust and useful lifetime. [Edge economics](EDGE.md)
explores these source ideas explicitly.

A service completion is often a maximum over required paths or the first eligible
alternative, with queues coupling those paths. Summing independently measured
hop percentiles or minimizing total edge cost does not optimize its tail. Finite
buffers, bursts and retry feedback can turn small average-demand changes into
large losses of deadline goodput. Stable total goodput can coexist with growing
backlog, and better aggregate p99 can hide worse service for an independent cohort.

## Adapt at several granularities using local evidence

The policy can choose whole invocations, logical fragments, chunks, batches,
streams, shards, hosts, subnets or regions. Granularity controls balancing,
compression, locality, route-state size, ordering/reassembly work and failure
scope. Per-message distribution can smooth load but lose batching locality;
coarse stripes can preserve locality while creating bursts or concentrated risk.
Hierarchical delegation compresses decisions but leaves someone responsible for
local coverage, failure and resources.

Randomized or deterministic entropy-derived choices, a small family of routes,
two-choice load probes, queue feedback, receiver-driven credits and explicit
ownership are different ways to make decisions with limited coordination. They
must distinguish traffic suppression from semantic authority. Shared entropy
cannot prove a selected sender is alive or make candidate lists agree. Sender
backups need usable copies and a progress rule; notifications take time and can
be lost, so redundant work can remain after logical completion.

Adaptation includes more than changing a tree: change a flow, batch size, work
grain, representation, compute placement, replica assignment, admission budget,
client ingress, retained state location or witness placement. Each has a different
switching cost and authority implication. A flow reroll can leave authority
unchanged; moving a witness's role may require history and an authority transfer.
An apparently idle destination can attract several controllers at once.

Compare policies using the observations they would actually receive, including
stale telemetry, missing receipts and measurement uncertainty. Instantaneous
global knowledge is a useful oracle comparator only when explicitly labeled.
Static candidates under fixed load are not evidence that an online controller
converges or avoids oscillation.

## Failures and membership change the outstanding work

A fault can interrupt input availability, transformation, persistence,
acknowledgment, publication, transfer or reclamation. Inject it at those causal
boundaries as well as on hosts and links. Delay, duplication, reordering, partial
results, stale control state, process/host/domain loss and resource exhaustion
produce different information at each actor.

Retry, alternate holders, duplicate execution, selective repair, coding,
snapshot/catch-up, durable result retrieval and explicit uncertainty are possible
responses. None repairs destroyed unique information or creates missing authority.
Two logical paths sharing one NIC or failure domain do not provide independent
protection. Retrying can worsen the resource shortage that caused the timeout.

The logical obligation survives a route's disappearance. A new physical replica
does not necessarily add new logical data to a read; a new subscription does not
necessarily require all past events. Define the required coverage and its boundary,
then retain or reconstruct enough information to meet it. Bounded retention,
unbounded lag and unlimited admission cannot all hold without another place to
store the accumulating obligation. Changes to representation also need a lifetime
for interpreters, dictionaries, row mappings and input versions.

## Use combinations to challenge the model

The useful next scenarios are combinations of dimensions, rather than another
list of topology names. Examples worth comparing include:

- A selective cross-shard read creates an exact selector, reuses it on resident
  replicas, materializes some results across an expensive link, and returns via
  a different server while one input version is being retired.
- A stream of tiny invocations expands into a skewed many-to-many shuffle plus
  narrow writes; a fast durable follower shares cores with the extensions, and
  the final client drains slowly. Compare work grain, placement and retention.
- Several shards adapt toward the same apparently idle gateway from stale
  observations. A node joins during route replacement, a sender loses its receipt,
  and old obligations must survive the transition without a retry storm.
- A partial aggregate travels around a feedback workflow while contributors or
  logical ranges move. Exact contributor replacement, empty completion and
  retained interpretation state matter more than the current VM graph.
- A small set of equivalent origins produce different usable representations at
  different times. One path is cheapest, another fastest, and a third has warmer
  recipient state; the selected origin fails after another origin's hedge starts.

These are exploratory combinations, not a roadmap or required architecture.
The current evidence already connects several dimensions: packet batching with
repair, constrained routes with selection granularity, reads with replication
placement, work grain with nonpreemption, enhancement with residency and edge
price, and membership with retained delivery knowledge. [Findings](FINDINGS.md)
records those observed tradeoffs. The larger abstraction explains what each
comparison covers and prevents a useful example from becoming an accidental rule.
