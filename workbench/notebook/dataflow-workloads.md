# Workloads that justify broader dataflow

[Retired-study context](retired-spikes.md) · Preserved research record; prototype names and results below refer to the archived source.

2026-09-26. These are discriminating workload shapes, not a proposed operator
catalog or a commitment to build a universal runtime. The exploration extends
the [dissemination workload survey](dissemination/SOURCE-SURVEY-WORKLOADS.md)
with feedback, stateful computation, dynamic discovery and application effects.
[Spark-like extension applications](#f19--spark-like-applications-over-reusable-distributed-data)
are explicit scope: code shipping and reusable distributed collections can
support substantial processing applications as well as individual UDF calls.
[Scope](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SCOPE.md) owns the module boundary; [sources](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md) records what
was inspected and the limited imports from primary literature. Examples below
are authored reasoning unless a specific executable source is named.

The strongest common need is to compose **bounded pieces of useful work with
explicit input meaning, output obligations and retained state**. A tree of
operators, a cyclic dataflow, a continuation and a stateful service can all expose
such pieces. Their semantic completion rules differ. Forcing them into one graph
language before understanding those rules would conceal the difficult cases.

## The distinctions that change the physical plan

| Application question | Why it changes execution |
| --- | --- |
| Is the answer a finite value, a revisable value, an append-only output or an action? | Early tentative values can feed private computation; an email, published row or external result cannot generally be retracted. |
| Which inputs are complete, at which version? | One snapshot, several declared source versions, an event-time window and a latest-value service are different contracts. Transport arrival does not select one. |
| Which state can be reconstructed? | Replaying immutable inputs, restoring maintained state and invoking a live external service have different recovery costs and meanings. |
| Is work divisible, and what does a partial represent? | Splitting rows, key groups, join pairs, iterations and application entities requires different identities and combination laws. |
| Is a consumer mandatory, optional or shared? | One slow optional subscriber need not determine foreground completion, but cancellation cannot discard another consumer's result or an owed effect. |
| What can grow before useful completion? | Fanout, a join product, an unbounded key set, model state, historical versions and unresolved effects can dominate the tiny initiating request. |
| Which location is authoritative, and which is merely convenient? | A cached model, resident index, permitted replica, effect authority and reachable response gateway constrain placement differently. |

These questions concern both SQL and non-database applications. Engine answers
them for database plans. An application adapter supplies other meanings. Loom
can choose among the permitted work placements and enforce physical budgets;
Orbital transports and retains opaque bytes and reports its own service facts.

## Finite parallel work and exchanges

### F01 — Partitioned scans and batches, including replicas inside one shard

**Requirement and flow:** a coherent report or feature extraction captures a
source cut, divides logical ranges across eligible replicas, and gathers or
reduces their outputs. Many short requests can instead be assigned whole.
**Alternatives:** source-local fused pipelines; disjoint range tasks; weighted
stealing of remaining ranges; gather raw inputs to a prepared consumer. Keep
physical chunking separate from logical coverage and snapshot lifetime.
**Counterexample:** two fast replicas returning the same range do not cover a
missing range. More pieces can make a cheap query slower through dispatch,
packet work and merging; a replica with the bytes but no usable version is not
ready. **Evidence/gap:** [existing read probes](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dissemination/READS-AND-EXTENSIONS.md)
check finite coverage and compare granularity, while supplying the cut and
replica readiness. They do not construct a general distributed snapshot.

### F02 — Repartition, gather and broadcast are placements of work

**Requirement and flow:** ingestion normalization, a group-by or a backend batch
routes compatible items to state indexed by a new key. Outputs may then go to a
different partitioning or directly to clients. **Alternatives:** partition both
inputs; broadcast a small immutable side; gather a small job to one prepared
worker; route requests to existing owners; maintain a reusable partitioned
intermediate. Host-local combination can reduce inter-host packet work.
**Counterexample:** broadcasting a 10 MB build to 100 workers creates roughly
1 GB of receiver copies before metadata and retransmission, even when the
source transmits through a cheap tree. A repartition can save copies but lose
locality, require a second exchange and spill. **Evidence/gap:**
[Volcano](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature) supplies the exchange separation;
[dissemination](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dissemination/FINDINGS.md) prices finite shared
resources. No measured SixDB join/exchange crossover is established.

### F03 — Distributed joins need alternatives beyond one hash shuffle

**Requirement and flow:** orders/customers enrichment, graph motifs or entity
resolution combine multiple relations with bag multiplicity and possible
unmatched rows. **Alternatives:** local or batched remote index probes, small-side
broadcast, partitioned hash or sorted merge, exact/approximate semijoin filters,
and multiway or factorized processing for suitable cyclic shapes. Selectors or
factorized match sets can be message enhancements.
**Counterexample:** a hot key with m left and n right rows owes m×n joined pairs
if all rows are requested. More workers cannot remove that output. Randomly
salting both sides independently loses pairs; replicate one matching side or
assign pair rectangles exactly once. A Bloom positive still needs verification;
outer/anti-join absence needs complete opposite-side coverage. **Evidence/gap:**
[Calico's join survey](../../../calico/design/prior-art/engine2-survey/joins-execution.md)
locates relevant ideas, but its superiority/adoption verdicts do not carry over.
[Free Join and skew studies](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature) justify comparing
alternatives, not choosing one distributed SixDB algorithm.

### F04 — Reduction, factorization and finalization have separate laws

**Requirement and flow:** count, financial totals, histogram construction or a
join followed only by aggregation can combine nearer inputs, leaving compact
partials across expensive edges. **Alternatives:** per-worker partials, regional
trees, direct updates, deferred finalization or an unexpanded factorized result.
**Counterexample:** a mean needs count as well as sum; summing per-worker means
is wrong for unequal populations. Duplicate join matches must retain their
multiplicity even if their common value is stored once. Floating regrouping,
overflow and per-item returned intermediate values can forbid a convenient
merge. Exact distinct or quantiles do not generally admit a bounded exact
summary independent of the data domain. **Evidence/gap:**
[aggregate maintenance](../spikes/aggregate-maintenance/FINDINGS.md) already shows that
less logical work can cost more construction CPU. [W12/W25](dissemination/SOURCE-SURVEY-WORKLOADS.md)
separate algebra from result representation; distributed carrier implementation
and byte/CPU crossover remain open.

### F05 — Ranked retrieval needs a bound on unseen work

**Requirement and flow:** search, recommendations or a priority service probes
partitions and refines candidates until the requested rank is established.
**Alternatives:** all-partition exact top-k; sorted streams with bounds; adaptive
fanout; exact refinement after approximate candidate retrieval; an explicitly
approximate answer. Disjoint rows with complete final scores and one total tie
order permit merging each partition's local top-k.
**Counterexample:** with additive scores, shard 1 has a=10,c=6 and shard 2 has
b=10,c=6. Merging local top-1 misses global c=12. Per-shard top-k is not sufficient
for cross-shard group totals. Learned estimates cannot certify that unseen
partitions lose; a join or late authorization filter can change the winners.
**Evidence/gap:** [threshold algorithms](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature) supply
one bound-based approach under specific access/monotonicity assumptions.
The [read catalog](transactions/reads.md) distinguishes
historical rank from an updating exact-winner operation.

## Feedback, discovery and continuing change

### F06 — Demand-driven graph and dependency traversal

**Requirement and flow:** a permission graph, bill of materials, service dependency
lookup or document-link expansion discovers new keys from earlier results.
**Alternatives:** frontier batches grouped by owner; continuations sent toward
resident data; local depth-first traversal; precomputed reachability for frequently
reused relations. Bound active fanout without making the work domain static.
**Counterexample:** an empty local queue while a remote lookup is outstanding is
not completion. A cycle requires application-defined duplicate/visit state;
deduplicating only by node is wrong when path, depth or accumulated state affects
the result. Late sources still need the chosen cut. **Evidence/gap:**
[archived ChainVM ideas](dissemination/SOURCE-SURVEY-ARCHIVE.md#a08--let-an-immutable-data-structure-also-supply-its-traversal-schedule)
offer local schedule/traversal reuse, not distributed termination or recovery.
This is a useful bridge between database probes and arbitrary backend requests.

### F07 — Iterative and recursive fixed points

**Requirement and flow:** reachability, strongly connected components, dependency
closure or an iterative solver feeds changed state into more work until a stated
condition holds. **Alternatives:** barrier-separated rounds; asynchronous worklists
with scoped progress; semi-naive delta rounds; incremental maintenance across
input revisions. A local loop can stay local when communication is unnecessary.
**Counterexample:** numerical residual below a tolerance, no new set members,
an iteration limit and distributed quiescence are different termination facts.
Non-monotone updates may oscillate. Waiting for an entire deployment each round
can import one slow region into unrelated computations. Conversely, unchecked
asynchronous updates may change floating results or convergence behavior.
**Evidence/gap:** [Naiad, Differential Dataflow and Pregel](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature)
demonstrate distinct feedback/progress approaches. Their iteration labels are
not Orbital transaction positions or evidence that output is durable. Full
general recursion need not become a native Orbital feature.

### F08 — Streaming windows, CDC and source closure

**Requirement and flow:** logs, sensor events or CDC enter through multiple
connectors, transform and enrich against state, then update a materialized product
or sink with a checkpoint. **Alternatives:** bounded microbatches; continuously
updated windows with corrections; source-qualified finite generations; replay
from a retained input log. Source order, event time and arrival time are distinct.
**Counterexample:** a runtime frontier closes registered input work; it does not
prove a disconnected sensor will never submit an older event. Advancing a source
offset before its output is recoverable can lose work. Retrying an old upsert
after a delete can resurrect a row without a retained sequence tombstone.
**Evidence/gap:** [worked snapshot/CDC activation](transactions/elt.md)
checks supplied source order, deduplication and checkpoint/output coupling;
connector gap detection remains outside that probe.
[The Dataflow Model](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature) separates output timing,
event-time grouping and correction policy.

### F09 — Incremental joins, negation and deletion-sensitive views

**Requirement and flow:** continuously maintained features, fraud relations or
unmatched-order alerts propagate changes through stateful joins and reductions.
**Alternatives:** immediate deltas, coalesced batches, selective recomputation,
periodic complete rebuild, or serving a declared older version while refreshing.
Retain reusable arrangements only where update/query savings repay state and
maintenance.
**Counterexample:** an insert can retract an anti-join result; deleting the
current minimum requires another witness. A single inserted dimension row may
match millions of facts, so small input deltas do not imply small work. Two-input
join updates need cross terms or a defined sequence that accounts for them once.
An emitted alert cannot be silently undone when its predicate later retracts.
**Evidence/gap:** [Differential Dataflow and DBSP](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature)
show expressive incremental formulations. They do not make arbitrary UDFs
cheaply incremental, bound every arrangement, or supply an external action
contract. A recompute alternative remains essential.

### F10 — Repartitioning a live computation is itself dataflow

**Requirement and flow:** a hot key, new replica or changed query plan moves state
and remaining work while inputs continue. **Alternatives:** move only unstarted
work; copy immutable state then catch up; split a legal key group; dual-run and
compare a bounded revision; or pause briefly at an application boundary.
**Counterexample:** sending future records to a new owner before its old state is
ready splits one aggregate. Accepting both owners' output can double count. An
old worker's late completion remains identifiable after reassignment; extra
replicas do not create extra logical contributions. A hot indivisible entity
may not become parallel merely because its queue migrates.
**Evidence/gap:** [W32/W49](dissemination/SOURCE-SURVEY-WORKLOADS.md)
record frontier-compatible plan changes and mapping cutovers. Dynamic state
migration, its temporary double residency and recovery obligations are still
unimplemented in the dissemination study.

## HTAP, ELT and publication

### F11 — Private bulk construction with a small publication boundary

**Requirement and flow:** a report, model, feature table or authoritative ELT
generation reads declared source versions, privately builds many objects, checks
them and publishes references. **Alternatives:** materialize reusable products;
stream one-shot results; incremental maintenance; versioned partition replacement.
Execution, storage and publication grains can differ.
**Counterexample:** atomically swapping a stale root can erase concurrent live
edits. A historical authoritative replacement and a preserving live MERGE have
different contracts. A small published pointer does not make referenced bytes
available or reclaimable: old readers, replay and incomplete readers still need
their versions. **Evidence/gap:** [worked ELT histories](transactions/elt.md)
exercise stale-root loss and preserving two-object publication. Multi-object
availability, long-lived reader retention and realistic build costs remain gaps.

### F12 — Broad read, narrow write, or dynamically expanding effect

**Requirement and flow:** a backend computes eligibility/risk/rank, then mutates
the chosen entities; an ELT program discovers destination rows or cascades.
**Alternatives:** one transaction over a complete possible-effect domain;
historical analysis followed by a bounded validated action; an explicitly
different generation-product contract. Parallelize private reads and computation
without changing the chosen promise.
**Counterexample:** a tiny final write does not imply a tiny dependency scope.
Late discovery of a source is different from adding an undeclared effect
authority. A child call waiting for its parent's publication deadlocks if the
parent needs that result to publish. Returning success before a required
constraint check changes the operation. **Evidence/gap:**
[allocation pipelining](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-scenarios/PIPELINING.md) removes a particular
execution-time exclusion while retaining pending effects; it does not make all
assignments blind. [Extension composition](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-scenarios/reconsideration/COMPOSITION.md)
keeps tentative internal work distinct from transaction publication.

### F13 — Online build, refresh and maintenance coexist with serving

**Requirement and flow:** build an index, backfill a derived column, compact state
or prepare a replacement model while reads/writes continue; then activate the
new version. **Alternatives:** snapshot plus captured tail, dual maintenance,
incremental build, or a bounded pause; retain old readers until retirement.
**Counterexample:** a complete scan with no update-capture coverage is not a
current index. Low-priority maintenance can starve until replay/history consumes
the pool and harms foreground service. Migrating physical bytes is not itself a
logical SQL mutation, but does need mapping and lifetime coherence.
**Evidence/gap:** [existing write scenarios](transactions/writes.md)
identify these contracts; [Loom's resource note](loom-objectives-and-architecture.md)
motivates jointly accounting for foreground and its maintenance debt. No
complete online-build or migration protocol is claimed here.

## Extension-hosted and non-database applications

### F14 — Backend request chains, rings and conditional fanout

**Requirement and flow:** ingress authenticates, a state-local handler fans out to
independent services, their results select the next request, and another server
returns the answer. Examples include checkout preparation, recommendation assembly
and a game-session request. **Alternatives:** fused local calls, futures over
immutable values, bounded channels, short actor calls or an explicit continuation
at a real wait. Do not create a network/task boundary for every function.
**Counterexample:** a logical fork/join can deadlock when parents occupy every
executor slot while children need those slots. Direct reply requires a valid
reachable return route; losing ingress does not authorize repeating an accepted
business action. A remote read's outcome can change the remaining dependency
graph. **Evidence/gap:** [ring dissemination](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dissemination/READS-AND-EXTENSIONS.md#ring-paths-and-backend-extensions)
and [Ray's tasks/actors](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature) provide useful contrasting
models. Neither selects a SixDB application framework.

### F15 — Durable business workflows with external actions

**Requirement and flow:** a committed decision creates an intent, a worker invokes
an external service, and a durable outcome permits the next step. Timers or human
responses can leave the workflow idle for a long time. **Alternatives:** small
transactional steps with explicit state; retained event histories; transactional
sinks; idempotency keys/status lookup; or application-defined compensation.
**Counterexample:** after a payment executes but its reply is lost, replaying a
pure-looking task can charge twice. A transport ACK, a workflow checkpoint and
the external effect are separate facts. Compensation is another action with
failure and business rules, not automatic rollback. Canceling the caller does
not undo an already committed intent. **Evidence/gap:**
[delivery/effect ambiguity](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dissemination/DELIVERY.md) and
[extension external-write guidance](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-scenarios/reconsideration/COMPOSITION.md)
cover the boundary. General workflow APIs, calendars and saga policies need not
be Orbital concepts; opaque durable state and recoverable delivery can support
an application-owned adapter.

### F16 — Stateful entities and hot services

**Requirement and flow:** per-user sessions, collaborative rooms, rate limits or
simulation entities own state and exchange events. **Alternatives:** move a
command to its owner, partition independent entities, replicate immutable query
state, or use an application-proven split/merge law for a hot entity.
**Counterexample:** serial ownership avoids some local races but does not make a
multi-entity invariant atomic. A hot room can dominate one owner; blindly cloning
mutable owners changes ordering and conflict behavior. Inter-entity calls can
form cycles even when each handler looks simple. An incarnation change must
not accept stale work as fresh state. **Evidence/gap:**
[Orleans and Ray](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature) motivate stateful placement
alongside task parallelism. Their guarantees are not inherited. Entity state,
transaction boundaries and recovery/replay meaning belong to the application.

### F17 — Feature, inference and learning pipelines

**Requirement and flow:** capture features, enrich or join them, run a versioned
model, rank results, then record feedback; training/simulation periodically
produces a new model. **Alternatives:** query-time features versus materialized
features; move compact inputs to warm model state; move small models to data;
batch compatible inference; deterministic finite training rounds or an explicitly
asynchronous learning contract. CPU/GPU/device placement is a possible workload
dimension, not a requirement to add those backends now.
**Counterexample:** batching can improve device use while delaying a short request;
cold model transfer can dwarf inference. Different feature cuts, model versions,
runtime numerics or external responses change replay. Asynchronous gradient
order may change the trained artifact. Speculatively canceling inference does
not necessarily stop the submitted device kernel. **Evidence/gap:**
[extension identity/verification](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-scenarios/reconsideration/COMPOSITION.md)
supplies current constraints; [TensorFlow and Ray](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#primary-literature)
motivate heterogeneous/data-dependent execution. No accelerator or training
performance claims are made for SixDB.

### F18 — Shared work and demand-dependent pruning

**Requirement and flow:** several reports, subscriptions or backend branches share
a scan, parsed document, dictionary evaluation or feature intermediate. A cheap
branch may make expensive siblings unnecessary. **Alternatives:** shared live
work, cached materialization, per-consumer recomputation or partial fusion. A
shared result may use different representations for different destinations.
**Counterexample:** canceling one subscriber does not cancel the others. Keeping
every slow subscriber attached can retain versions/buffers indefinitely; detaching
one requires a stated replay/failure contract. An absorbing Boolean input can
remove demand; XOR has no such shortcut. Comparing only final identical bytes
does not permit skipping mandated independent executions/checks.
**Evidence/gap:** [archived symbolic pruning](../../../consurgent/archive/notes-v0/LOGIC_symetric_binary.md)
and [W03/W34/W48](dissemination/SOURCE-SURVEY-WORKLOADS.md) supply the
ideas; the [enhancement study](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dissemination/ENHANCEMENT.md) demonstrates
resident versus cold consumer reversals. General distributed demand ownership
and multi-query cache admission remain open.

### F19 — Spark-like applications over reusable distributed data

**Requirement and flow:** an extension-hosted application defines a lazy data
preparation pipeline, reuses its partitioned output for several analyses or
learning iterations, and requests a result or publication. A log-processing or
feature-engineering application can combine relational stages with user-defined
partition functions and continuing input. This is more than a scalar SQL UDF.
**Alternatives:** a structured plan visible to Engine; an application-owned
collection/partition adapter with opaque functions; eager small jobs; reusable
materialized datasets; or live stateful processing. Ship pinned code/runtime,
captured values and immutable artifact references toward suitable data, or move
data toward an expensive prepared model. Use bounded channels within useful
regions and deliberate shuffle/spill or recoverable boundaries between them.

**Counterexample:** a cached partition is not a durable published product.
Recomputation from a recipe fails if its exact inputs, code or dependencies have
been discarded, and produces a different result if it calls a live service.
Collecting all output at one coordinator can exhaust its memory. Large captured
closures can defeat data locality; an opaque UDF can hide selectivity, expansion
and illegal effects from the planner. One long job need not be one long atomic
transaction: reading retained snapshots, publishing generations and taking short
live actions are different explicit choices. Canceling an action stops demand
subject to real task/I/O retirement and other consumers; it does not reverse
already published transactions.

**Evidence/gap:** the [official Spark sources](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/orbital-dataflow/SOURCES.md#spark-like-application-angle)
ground the comparison of lazy plans, partitioned functions, reuse, shuffle and
streaming state. SixDB's stronger integration opportunity is our hypothesis:
reuse its data versions, placement and publication mechanisms instead of always
exporting to a separate processing cluster. The spike must price isolation,
artifact distribution, version retention and interference with short transactions.
It does not establish Spark compatibility, a chosen client language, arbitrary
closure serialization or an intention to reproduce Spark's runtime.

## How these shapes should narrow the investigation

The worthwhile common composition is smaller than the full catalog: local
execution regions; selected bounded exchange/materialization boundaries;
application-owned state and progress; and identity/ownership that survives
reassignment, cancellation and recovery. Specialization can remain above those
mechanisms. A keyed stream adapter, a finite relational plan and a resumable
workflow need not expose the same programming model.

Four contrasts offer unusually strong signal for focused prototypes:

1. **Fuse, channel or materialize.** Include a slow branch, a shared consumer,
   output expansion and failure after partial output. Measure retained bytes
   over time and actual cancellation retirement, not only peak queue size.
2. **Partition or replicate.** Compare a small build, skewed keys and a large
   join product; check coverage against a sequential oracle. Charge all receiver
   copies, filter/build CPU, spill and return traffic.
3. **Recompute or maintain.** Include a selective insert, a delete invalidating a
   witness, and one small delta with a large affected region. Record retained
   arrangements and correction work, including work that never publishes.
4. **Finish or continue.** Include dynamic child discovery, a false quiet period,
   a late input correction and an external effect with an ambiguous outcome.
   Distinguish useful output, closed coverage, durable state and retired work.

These are comparisons, not a mandatory benchmark preset list. Ordinary point
work is a necessary control: if expressing a direct local request requires a
global graph, a persistent per-kernel task or a full dataflow progress protocol,
the composition has become broader than the evidence warrants.

The remaining scope is deliberate. SQL semantics, connector protocols, general
workflow languages, model training algorithms, arbitrary automatic incremental
UDF transformation and complete graph optimizers stay with their owning layers.
This exploration establishes which requirements their execution can expose; it
does not move their meaning into Orbital or promise a single best plan.
