# Recorded workload, representation and execution ideas

2026-09-26. This is a source audit and analytical exploration for Orbital
dissemination, not an implementation proposal or a new measurement. Each W ID
separates the recorded idea from its generalization here, a condition favoring it,
and a counterexample or cost. Sources are linked beside the idea. The inspected
inventory and exclusions below delimit the claim: this is not an assertion that
every linked implementation, paper or historical Calico experiment was audited.

The common decision is broader than choosing a communication tree. Choose which
work executes, where its inputs and retained state reside, which sufficient
representation crosses each boundary, who must independently execute or verify,
who sends, and what permits release, publication or reclamation. An edge can
shrink or expand bytes; it can also introduce address discovery, verification,
maintenance or future replay obligations. Minimize useful-completion latency and
total resource/price costs subject to those obligations, rather than optimizing
only a byte graph. These choices form a constrained family of alternatives;
there is no universal weight that exchanges away a missing result or effect.

Evidence names below: **read** is [read_scenario.py](read_scenario.py), whose fixed
cut and finite coverage are supplied facts and whose separate synthetic scan
resource does not implement a table operator; **mixed** is [MIXED.md](MIXED.md),
the shared CPU/memory/NIC read-work and replayable-write-effect proxy, explicitly
not a write-consensus implementation; **selection** is
[ENHANCEMENT.md](ENHANCEMENT.md), exact selection transport with declared residency;
**analytical read** is [READS-AND-EXTENSIONS.md](READS-AND-EXTENSIONS.md) and
[read_probe.py](read_probe.py). Existing source-spike timings remain conditional
local measurements. None becomes a distributed p99.9 result through citation.

## Reads, evidence and reuse

### W01 — Coherent reads, sessions and late-discovered shards

**Recorded:** [Read catalog, cases 1–3 and 18–20](../orbital-scenarios/reconsideration/READS.md)
separates historical cuts, updating transactions and publication-time promises.
A report header, pages and details can need one cut across many statements.
**Generalization:** distribute work while retaining cut-qualified coverage and
inputs, including shards discovered later. This removes live source exclusion
for historical work. Independently freshest shard reads can tear a transfer;
reopening a snapshot per page can disagree with the header. Retention, cut
acquisition and expired-history outcomes are part of cost. **Evidence:** read
checks finite chunk completion at an authored cut; neither it nor selection
constructs the distributed cut or serves versions to late discovery.

### W02 — Whole-query dispatch versus dividing one query among replicas

**Recorded:** [Reads and extensions](READS-AND-EXTENSIONS.md) separates
query-serving parallelism from mandated repeated fold/check executions.
**Generalization:** allocate disjoint logical chunks across available replicas
inside each shard and reduce across shards; dispatch whole queries when their
setup, residency and small size favor it. Division helps a large divisible scan;
one lagging replica or a large reduction can make equal splitting slower than
excluding it. Replicas with different layouts need common row identities, not
identical physical chunks. **Evidence:** read exercises pieces, shared workers,
readiness and hedges, with exact finite coverage. Its queue-ready selector has
instantaneous global knowledge; distributed observations, skewed operators,
version acquisition and dynamic stealing remain gaps.

### W03 — Shared scans, dictionaries and common subexpressions

**Recorded:** [Row-filter design](../row-filter-signatures/design.md),
[FSST follow-up](../regexp-lowering/fsst-follow-up.md) and
[value reuse](../ikea-composition/value-reuse-sketches.md) consider shared scans,
distinct-value evaluation and one producer feeding several consumers.
**Generalization:** compute a reusable artifact once for compatible queries,
then distribute per-query selection/result state. It pays when repeated values
or shared expensive predicates dominate; it loses for one-shot queries, divergent
cuts or a slow subscriber retaining the shared buffer. Query cancellation must
release one reference without destroying another consumer's work. **Evidence:**
native reuse has local probes; selection shares one predicate across recipients.
No current network case models cross-query cache admission, reference counts,
eviction, dictionary misses or different query completion lifetimes.

### W04 — Exact selection, necessary evidence and unanswered coverage

**Recorded:** [Secondary summaries](../../notebook/secondary-summaries.md) and
[row-filter design](../row-filter-signatures/design.md) distinguish proof of
absence, estimates and exact/possible row truth. **Generalization:** place a cheap
necessary filter before expensive decode, transfer or join work; carry residual
obligations and complete domain coverage explicitly. Benefit requires downstream
cost saved to exceed filter/encoding/verification cost. A positive fingerprint
does not prove equality, an empty missing partition is not a completed empty
result, and SQL NULL prevents naive complement. **Evidence:** selection implements
exact bitmap/IDs/spans and checks lost empty output; row-filter probes cover
soundness and local cost. A transported approximate-evidence pipeline remains
unimplemented.

### W05 — Place Boolean fragments, not isolated predicate atoms

**Recorded:** [Row-filter design](../row-filter-signatures/design.md) and
[layout analyser](../layout-analyser/design.md) carry certain/possible truth and
unresolved branches. **Generalization:** select relay work by the Boolean fragment
its resident fields can actually resolve, including exact early acceptance for
OR branches. A plane containing A and B cannot reject (A AND B) OR C when C is
unknown; moving two cheap tests there merely adds bytes and stages. Grouping
co-used features can avoid later work but duplicates maintenance and may enlarge
unconditional reads. **Evidence:** the resident filter study has the tautological
projection counterexample. Selection transports one exact predicate; no network
case carries a factored residual program or branch-qualified evidence.

### W06 — Same-row rollups, positional summaries and frontier reduction

**Recorded:** [Rollups](../row-filter-signatures/rollups.md) compares shared OR,
joint code presence, marginal presence, intervals, pattern lists/antichains and
positional bits. **Generalization:** aggregate at a frontier only with an operation
preserving the required query fact. Joint presence can avoid downstream rows
where marginal A and B occur on different rows. Larger groups amortize metadata
but saturate evidence; tiny groups can cost more summary bytes than input.
Overflow may conservatively coarsen rejection evidence, not silently certify it.
**Evidence:** resident findings compare selected block sizes and representations;
network fan-in aggregates authored payloads without implementing these semantic
merge laws, row movement or delete-witness repair.

### W07 — Sketches, filters and histograms need different maintenance

**Recorded:** [Secondary summaries](../../notebook/secondary-summaries.md)
keeps absence proofs, selectivity estimates and approximate answers distinct;
the intended meaning of “min-sketches” is unresolved. **Generalization:** share
input scans or placement where useful without forcing one retention/merge rule.
A stale histogram can remain useful cost evidence; a stale negative filter
missing an insertion can make an exact result wrong. Correlated fields defeat
simple selectivity multiplication, while counting/deletion and incompatible
hash/bin editions introduce state. **Evidence:** these distinctions are recorded
reasoning, with related row-filter measurements. No distributed sketch error,
merge compatibility, deletion or estimation-feedback scenario exists here.

### W08 — Delta coalescing can remove work and still cost more

**Recorded:** [Aggregate findings](../aggregate-maintenance/FINDINGS.md) found
71% fewer upper-level adjustments but about 24 times the resident CPU cost for
one location-run construction path. **Generalization:** combine updates before
an expensive remote edge, lock or storage write when duplicate contributions
are common and the downstream cost dominates. Charge sorting, allocation,
construction, metadata and retained inputs. A resident cheap counter with
dispersed updates can favor direct updates even with more logical operations.
**Evidence:** aggregate probes compare real local construction and replay;
network fan-in explores authored aggregation and packet service. It does not
implement those builders or establish their CPU/byte crossover over a WAN.

### W09 — Choose where deltas are addressed and where correction occurs

**Recorded:** [Aggregate design](../aggregate-maintenance/design.md) compares
ancestor-addressed fanout, location-addressed runs, intermediate rollups,
producer lanes, mutable trees and selected summaries plus tails.
**Generalization:** exchange write-side fanout for query-side merging at chosen
spatial and time frontiers. Location runs help when many ancestor writes collapse;
per-producer lanes reduce write contention but multiply read fan-in. A range
being covered spatially does not mean its run covers the requested visibility
cut. Incompatible base/run manifests double count compacted contributions.
**Evidence:** local aggregate probes have bounded exact integer histories;
current dissemination has no spatial-by-visibility run index, concurrent
compaction manifest or snapshot-qualified correction traffic.

### W10 — Idempotent dirty transitions and bypass as useful messages

**Recorded:** [Dirty-buffer proposal](../aggregate-maintenance/shared-dirty-buffer.md)
and [findings](../aggregate-maintenance/dirty-buffer/FINDINGS.md) exploit a first
dirty transition followed by read-only repeated marks. **Generalization:** send
an invalidation/unknown-evidence transition once, then batch subsequent facts
until repair. This can reduce hot shared metadata and control packets; dispersed
first touches, reset scans and broad-query saturation can dominate. A positive
dirty filter should not always trigger a flush: false positives may make doing
nothing or scanning the small tail cheaper. **Evidence:** hot and dispersed
local controls include the essential no-filter buffer. Current network cases
do not implement dirty-generation reset, reader bypass or correction-triggered
repair.

### W11 — Shared buffers, holes and coalesced read pressure

**Recorded:** [Shared dirty buffer](../aggregate-maintenance/shared-dirty-buffer.md)
proposes producer span reservations, shared append, prefix correction caching
and locally combined read-pressure signals. **Generalization:** reserve coarse
space, publish independently valid contributions and scan once for several
waiting requests. It amortizes reservation and repeated correction work.
A stalled producer can leave holes that stop a contiguous frontier; cached
corrections need prefix and generation identity. Flushing every positive probe
can turn saved writes into more traffic. **Evidence:** local reads/replay occur
at quiescent boundaries; network frontier tests model explicitly supplied
completion, not concurrent buffer-hole publication or shared query correction.

### W12 — Numeric laws and observable outputs constrain aggregation

**Recorded:** [Aggregate semantics](../aggregate-maintenance/design.md),
[operation granularity](../ikea-composition/operation-granularity.md) and
[write catalog](../orbital-scenarios/reconsideration/WRITES.md) distinguish exact
count/sum, extrema, floating grouping and intermediate outputs.
**Generalization:** reduce only under the full result/effect contract. Exact
completion-only deltas may combine; returning each previous total fixes more
order, and deleting the last minimum needs another witness or conservative
repair. Disabling fast-math does not make arbitrary regrouping equivalent.
**Evidence:** fixed/fold histories and local bounded integer probes contain
positive and negative examples. Generic network aggregation has no floating
reproducibility, top-k deletion or arbitrary UDF law.

## Representation and address dependencies

### W13 — Jointly choose representation, access program and lifetime

**Recorded:** [Layout-analyser design](../layout-analyser/design.md) and
[case study](../layout-analyser/case-study.md) separate features, planes,
decoding, address discovery, maintenance and physical placement.
**Generalization:** choose the representation used on each edge with its complete
consumer path and lifetime. A compact hot prefix can reject before expensive
tails, but duplicating it costs mutation, storage and replay bytes; late projection
may still require all tails. A “compressed edge” can add metadata fetches or
expansion at the receiver. **Evidence:** this is a design/case-study framework
with related codec measurements. Selection models a small exact encoding choice;
the simulator does not model the full representation/access/maintenance graph.

### W14 — Preserve heterogeneous actual representations and lazy migration

**Recorded:** [Second composition sketches](../ikea-composition/sketches-2.md)
allows different segment representations indefinitely; preferred layout does
not reinterpret stored bytes. **Generalization:** move executable work to an
existing useful replica layout, rebind readers without rewriting, encode future
data differently, or migrate selected old regions. Separate those four actions
and their costs. A hot specialization can repay migration; a cold region may
never repay copy, doubled retention and compatibility code. **Evidence:** Ikea
supports nested substitutions and retained bindings; network fixtures assume
homogeneous synthetic scan rates or declared resident inputs, with no mixed
representation negotiation, conversion or cutover.

### W15 — Derivable names can remove serial address discovery

**Recorded:** [Prefix addressing](../../notebook/prefix-addressing.md)
separates natural routing names, stable identity, local resolution and payload
order. **Generalization:** derive several target names before parent data
arrives, batch resolution/prefetch and route logical ownership once before local
lookups. This can shorten dependent RTTs. Enclosing prefix boxes need exact
ownership fences: the longest matching box can name the wrong segment, and
rejection is not absence. Loose stable boxes reduce rename churn but add
candidate work. **Evidence:** this is recorded analysis informed by local
remapping probes. No network experiment compares dependent lookup against
batched enclosure discovery with mapping changes.

### W16 — Remapping contains some repair fanout, not all movement

**Recorded:** [Trie design](../trie-remapping/design.md) and
[findings](../trie-remapping/FINDINGS.md) compare natural routing, ordered gaps,
rank-packed columns, block-local disorder and separate payload pools.
**Generalization:** trade local indirection, compression and scan order against
mutation/copy/remote-index repair. Stable labels do not stop dense rank columns
shifting; stable row IDs cut locator repairs but retain payload movement.
Append-aware slack helps appends and fails moving hotspots. **Evidence:** the
resident probe measures these reversals and excludes concurrent publication,
WAL and replication. Dissemination needs explicit mapping-qualified selectors
and deltas; selection checks mapping identity but performs no live remap.

### W17 — Sparse execution still owes reconstruction and addressing inputs

**Recorded:** [BEC metadata findings](../bec256-composition/metadata-findings.md)
and [operation granularity](../ikea-composition/operation-granularity.md) retain
preceding lengths, restart/carry and per-source metadata.
**Generalization:** fetch dependency closure, not merely selected output rows.
Checkpointed lengths shrink metadata but fresh random access may traverse
inactive predecessors; a sequential cursor can retain an offset frontier.
Predictive residuals similarly need source fields and can amplify source updates.
**Evidence:** local point/range and packed-directory probes price some forms;
the network read model treats chunks as independently available and does not
represent pointer/length/restart-dependent remote fetches or checkpoint placement.

### W18 — Estimates can decline work; exact sizes can reserve it

**Recorded:** [PFOR bounds](../../notebook/pfor-bounds.md) and
[Bec256 use](../../../ikea/docs/bec256/usage.md) distinguish cheap estimates,
proofs, exact preparation and encoded footprint.
**Generalization:** gate expensive encode/transfer choices with a cheap statistic,
then reserve from a valid bound or exact prepared size. Favor it where rejected
encodes are costly and fallback is cheap. Exception count or estimated bytes
does not prove the winning physical format; padding makes physical cost
nonmonotone, and BEC's 32-byte input can expand to 47 bytes. **Evidence:** existing
codec probes and contracts cover these local distinctions. Network payload
sizes are authored, not produced by live compression estimators.

### W19 — Bound survivor regions instead of shipping whole values

**Recorded:** [Survivor bounds](../regexp-lowering/survivor-bounds.md) asks
whether necessary matches can identify regions for exact verification.
**Generalization:** send or decode a union of complete candidate intervals when
long values contain short matches. Charge position discovery, code-symbol
boundaries, context, overfetch and extra calls. A first false witness followed
by a true one defeats first-witness truncation; concatenated slices invent
adjacency, and anchors can change meaning on a slice. **Evidence:** raw-string
efficacy/verification is measured, compressed bounded decode is deferred.
Selection operates on whole rows; no network case transports contextual
sub-value intervals with completeness and fallback.

### W20 — Factored filters retain relationships without multiplying scans

**Recorded:** [Regexp closeout](../regexp-lowering/CONCLUSIONS.md) and
[factored findings](../regexp-lowering/FACTORED_FINDINGS.md) preserve OR/sequence,
relative order and selected richer constraints.
**Generalization:** share leaf scans and send compact unresolved structure
between useful refinement points. Factoring helps repeated fragments; order can
remove many candidates, but character constraints solve different failures.
More stages lose when a cheap first filter already finds all actual matches.
Fewest survivors is not least total CPU or latency. **Evidence:** raw operation
counts and semantic checks exist; they are not CPU/network speedups. Shared
compressed transitions, bounded compilation and distributed residual execution
remain gaps.

### W21 — Per-table preparation, dictionary scope and resident-state placement

**Recorded:** [FSST follow-up](../regexp-lowering/fsst-follow-up.md) compares
many small tables, few large tables, cold binding, warm reuse and per-distinct-
value evaluation. **Generalization:** place compute where code/dictionary/model
and exact input versions already reside, or ship prepared reusable state once.
It helps repeated queries; one-shot or frequently rotating patterns/tables
can spend more on preparation and cache churn than execution. A matcher bound
to the wrong table is invalid, not merely slower. **Evidence:** the source
explicitly defers compressed execution. Selection declares residency but does
not charge its construction, retention, cache misses or multi-query eviction.

## Grain, ownership and shared resources

### W22 — Physical packets, work grain and publication are independent

**Recorded:** [Operation granularity](../ikea-composition/operation-granularity.md)
and [SeriesPack findings](../ikea-composition/native-regions/findings.md) separate
semantic group, physical tile, working lanes, observation and scheduling grain.
**Generalization:** batch across compatible physical pieces to amortize execution
or packets without forcing a bigger commit/observation boundary. Large chunks
help throughput but extend nonpreemptive blocking; indivisible transforms and
last partials constrain splitting. **Evidence:** local grouped consumers have
conditional wins; mixed shows a large-read chunk delaying foreground completion
while smaller chunks cost more background work. Neither establishes a universal
batch size or links vector width directly to message size.

### W23 — Native handoff, shared compiled regions and stored chunks compete

**Recorded:** [Composition design](../ikea-composition/design.md),
[operation granularity](../ikea-composition/operation-granularity.md) and
[build lessons](../../notebook/build-iteration.md) compare fusion, curated
regions, CPS and explicit materialization. **Generalization:** avoid repeated
decode/stores on immediate local edges, while factoring reusable code to control
compilation products. Stored chunks earn their cost across real suspension or
delayed fanout. An exposed logical node need not be a scheduler task or callable
continuation; caller live state can spill even when payload crosses in registers.
**Evidence:** local comparisons have both native wins and materialized winners.
The network core represents compute service but not these compiled boundaries.

### W24 — Unequal grains and two-source operations need coordinate ownership

**Recorded:** [Operation granularity](../ikea-composition/operation-granularity.md)
works through 16-to-8, 16-to-32, 37-value tails, bitset pairs and partial consumers.
**Generalization:** move partial work with exact source/output coordinates,
remaining extent and an owner for unused values. Incremental reduction can
avoid a wide temporary; a genuinely indivisible consumer needs gathering or
another implementation. Equal counts do not align compacted row lists; two
256-position operands are not automatically one 512-position sequence.
**Evidence:** heterogeneous local probes check several unequal-grain compositions;
read checks whole chunk identity only. Network joins, sorted merges and partial
consumption across failed/reassigned workers remain unexplored.

### W25 — Change result representation and finalization placement separately

**Recorded:** [Native-region findings](../ikea-composition/native-regions/findings.md)
compares immediate scalar reductions, owned native partials and materialized
arrays. **Generalization:** defer finalization across local compatible consumers
or frontier reducers, then normalize at the actual external boundary. This
can remove repeated horizontal reductions; retaining large carriers or making
a remote consumer decode a proprietary form can lose. A partial sum represents
one result, not a collection of selected rows. **Evidence:** grouped/read and
deferred-result effects are measured locally and vary by target. Generic
network reduction does not implement those carrier laws or finalization costs.

### W26 — Consumer-shaped packet grouping and independent child placement

**Recorded:** [Tuple groups](../tuple-layout/batching/groups/README.md),
[head projection](../seriespack-head-projection/findings.md) and
[TuplePack extension guide](../../../ikea/docs/tuplepack/extending.md) retain
co-used value pieces and independently strided children.
**Generalization:** pack for downstream computation and fold pure wiring into
prepared routes when possible. Blanket transposition can split useful values;
requiring both children contiguous can prevent coalescing in the still-dense
child. Ordinary small GPR consumers can lose despite native large-packet wins.
**Evidence:** all are concrete local comparisons; selection chooses message
encoding by bytes, not receiver-native layout. Branch-specific packet layout,
decode cost and gather locality are unmodeled.

### W27 — Effect batching has its own footprint and completion boundary

**Recorded:** [Ikea integration](../../../ikea/docs/integration.md),
[Tuple maintenance](../tuple-layout/maintenance-review.md) and
[collector measurements](../ikea-composition/native-regions/findings.md)
distinguish logical destinations, issued physical bytes and maintenance reads.
**Generalization:** batch qualified effect records and reuse before/after values
without forcing a before-image array. Coalescing helps repeated metadata
operations, but wide preserving stores and persistent summary writes still
need coverage. An untouched field may be required to recompute a signature;
no payload store for an empty/full BEC transition does not mean no logical
effect. **Evidence:** local journals/checks exist. Network write payloads do not
derive physical/semantic effect closures from actual kernels.

### W28 — Resource admission must include future outputs and resolution

**Recorded:** [Loom notebook](../../notebook/loom-objectives-and-architecture.md),
[Ikea integration](../../../ikea/docs/integration.md) and
[pending-version study](../orbital-scenarios/PIPELINING.md) identify coupled
input/output and metadata budgets.
**Generalization:** reserve a progress path for expansion, spill, output and
resolution before retaining resources that can block it. This prevents every
operator holding input while waiting for output, or a pending writer waiting
for memory that its blocked reader must release. Reservations can underutilize
memory and reject otherwise feasible work; conservative bounds must be priced.
**Evidence:** mixed has input/output credits and refusal, but no multi-operator
wait-for graph or pending-version-memory deadlock scenario.

### W29 — Direct execution, DRAM interleaving and real waits are distinct

**Recorded:** [Loom architecture](../../notebook/loom-objectives-and-architecture.md)
separates native execution, short memory rings and completion-driven I/O.
**Generalization:** expose useful independent work while bytes arrive without
turning each kernel leaf into an asynchronous task. Ring slots help overlap
dependent memory accesses; a prefetch is not proof of readiness, and increasing
slots can overload translation/cache resources. A synchronous immediate borrow
cannot survive arbitrary suspension. **Evidence:** memory-characterisation
measures conditional concurrency; read/mixed model queued services, not memory
prefetch completion, actual ring cursors or scheduler hot-loop overhead.

### W30 — Move work, data, allocation or resumption home

**Recorded:** [Loom notebook](../../notebook/loom-objectives-and-architecture.md)
lists moving tasks, batches, requests, data and future allocation, and alternative
resumption pools. **Generalization:** choose movement by total locality and
resource cost, including where continuation state and output consumers live.
Moving a small continuation to resident data may beat copying inputs; moving
everything to one hot owner can create contention and fairness problems.
**Evidence:** mixed compares shared cores, separate cores and separate hosts;
the last adds NIC/memory/queue resources as well as CPU separation. It assumes
resident inputs, so it does not price migration, cold caches or changing
continuation home.

### W31 — A same-host edge is conditional, not a universal 100 ns link

**Recorded:** [Memory findings](../memory-characterisation/FINDINGS.md) and
[method, cache/NUMA section](../memory-characterisation/METHOD.md#cores-caches-and-numa)
measure clean/dirty peer access, SMT interference and distinct LLC domains.
**Generalization:** choose reader fanout and producer/consumer ownership batching
using actual cache domains, not just AZ/VM/NUMA labels. Shared read-only data can
be cheap; a later writer invalidating many readers or crossing LLC domains can
cost more than a cold load. **Evidence:** finite host measurements support this
qualification, not a particular internal cache route. Simulator memory edges
are authored service assumptions; reader-sharer counts, coherence and ring
distance do not feed back into them.

### W32 — Retained plans can improve without changing semantics or bytes

**Recorded:** [Plans notebook](../../notebook/ideas.md#plans-that-keep-improving),
[first sketches](../ikea-composition/sketches.md#clarification-analysis-and-execution-surfaces)
and [second sketches](../ikea-composition/sketches-2.md) keep equivalences,
applicability and conditional costs separate.
**Generalization:** reuse alternatives and observations; switch remaining query
work at a compatible completed frontier. It helps long scans under drift.
Changing a live stage table or interpreting retained state under a new layout
can duplicate/omit work. Probing every query can cost more than savings, as the
regexp warmup counterexample shows. **Evidence:** prototypes support local
alternatives, but no network case migrates a running query revision with
snapshot, output and aggregate state.

### W33 — Cost evidence expires differently from legality

**Recorded:** [Executable-placement result](../executable-placement/evidence/checked-point-layout-20260911/summary.md)
shows unchanged instruction bodies changing timing after relink; the
[layout analyser](../layout-analyser/design.md) separates reusable training,
local fitting and held-out evidence.
**Generalization:** retain provenance and conditions with cost models, and
revalidate resource choices when executable layout, host interference or workload
changes. A valid codec does not become invalid because its timing changed.
Overreacting to noisy last winners can oscillate placement and increase traffic.
**Evidence:** local placement restoration is a bounded counterfactual, not a
padding policy. Adaptive network cases do not calibrate real operator CPU cost
or jointly adapt host, plan and representation controls.

## Extensions, publication and changing data

### W34 — Relay enhancement may shrink or expand each branch differently

**Recorded:** [Selection study](ENHANCEMENT.md) and
[reads/extensions](READS-AND-EXTENSIONS.md) compare local computation, blocking
shared work and optional side information.
**Generalization:** choose compute and representation per destination using
exact input residency, selectivity, CPU pressure and edge price. A cold public
consumer with sparse matches can benefit greatly from selected rows; resident
consumers already have no base transfer to save, so a shared selector can reduce
CPU while increasing bytes and latency. Optional sidecars can duplicate all
compute. **Evidence:** selection executes these exact comparisons and stale-base
cases; arbitrary partial plans, joins, aggregate artifacts and reusable
cross-query results remain distinct unimplemented transforms.

### W35 — Ring requests and backend extensions change the critical path

**Recorded:** [Reads/extensions](READS-AND-EXTENSIONS.md) and
[composition](../orbital-scenarios/reconsideration/COMPOSITION.md) allow
extension-to-query-to-extension-to-write chains and a different response sender.
**Generalization:** place the next computation and result authority near useful
state rather than force a response through the ingress server. This can remove
a return hop and request reconstruction. A dynamic query can discover another
shard and another sequential dependency; an internal handler must use parent
transaction context, not wait for its parent's publication. **Evidence:** read
has dispatcher/worker/reducer/client rings; historical composition has exact
authored chains. Neither models general backend recursion or arbitrary
extension request DAGs.

### W36 — Required executions, checked requests and transmitters are separate

**Recorded:** [Extension composition](../orbital-scenarios/reconsideration/COMPOSITION.md)
requires applicable transaction-scoped checks and examines the first
authoritative shared action.
**Generalization:** choose senders among eligible holders without replacing
mandated independent checks by copied results. Checking once after private
computation can save rounds; a divergent query that changes shared observation
bounds may require checking its request prefix first. Equal final “OK” bytes
can conceal different authoritative actions. **Evidence:** semantic context
probes exist; the historical epoch-wide verification gate is explicitly
superseded. Dissemination does not implement the corrected verifier/epoch
protocol. See the network and delivery surveys for origin and completion rules.

### W37 — Extension state, code and external facts have explicit replay lives

**Recorded:** [Composition](../orbital-scenarios/reconsideration/COMPOSITION.md)
distinguishes disposable process memory from versioned dictionaries, model
state, connector offsets and captured live-service responses.
**Generalization:** keep reusable derived caches warm where possible, but retain
exact inputs/code/runtime identity needed to reproduce committed work. Shipping
a recipe/reference can save repeated payloads while increasing dependency
fetches and retention. Recalling a live service during replay or binding the
latest extension version changes the computation. **Evidence:** current probes
assume immutable functions and checking facts; no executable/artifact cache,
runtime restore or external-input acquisition protocol is implemented.

### W38 — Asynchronous audit trades latency for exposure and retained history

**Recorded:** [Approved execution with asynchronous checking](../orbital-scenarios/reconsideration/COMPOSITION.md#approved-execution-with-asynchronous-checking)
permits background checking under the approved contract.
**Generalization:** schedule audit as a resource class with explicit coverage,
delay and reproducibility debt. It can preserve foreground latency, but
sampling checks a different set than full replay; backlog grows exposure and
history requirements. A postcommit mismatch cannot retroactively abort the
transaction or safely replay external effects. **Evidence:** this is a
documented proposal/obligation, not an implemented audit scheduler. Mixed's
background work is not evidence of audit coverage, adjudication or recovery.

### W39 — A small operation can stand for many writes

**Recorded:** [Write catalog](../orbital-scenarios/reconsideration/WRITES.md)
separates private construction, logical description, dependency, publication
and work before success. **Generalization:** ship ordered transformations,
range tombstones or exact deltas where their semantics suffice; materialize
pages later. This helps regular large changes and avoids immediate page traffic.
The pending program transfers work to reads/index correction/compaction; exact
RETURNING, constraints or errors may require full evaluation before success.
An idempotent assignment replayed late can overwrite a newer value.
**Evidence:** fixed/fold histories include several laws and counterexamples;
network payloads do not implement lazy programs, tombstone membership or
reader-driven materialization.

### W40 — Output uncertainty and index placement determine fanout

**Recorded:** [Convergence](../orbital-scenarios/CONVERGENCE.md) and
[worked ELT](../orbital-scenarios/ELT-WORKED.md) distinguish row-owned indexes,
predicate authorities and complete possible-effect envelopes.
**Generalization:** optimize the declaration and authority graph as well as the
payload graph. Partition-local indexes keep updates local but fan queries out;
global value-partitioned indexes can reduce query fanout but need advance
coverage of possible destinations. Calling an index derived does not inform a
remote absence reader. **Evidence:** small semantic audits distinguish known
and unknown values; current network topologies do not change when a computed
value alters the authority set or measure distributed predicate waiting.

### W41 — Release allocation early, retain ordered pending outcomes

**Recorded:** [Pipelining](../orbital-scenarios/PIPELINING.md) releases allocation
after positions are announced, before computation. **Generalization:** separate
short ordering/space operations from long payload work wherever the application
contract permits, while provisioning the pending metadata and outcome path.
Independent complete replacement may proceed; a read-modify-write still awaits
its true predecessor. No-write is not a tombstone, and installation order is
not logical order. **Evidence:** the source has bounded semantic/timing probes,
including worse deadline results and longer pending chains. The network mixed
proxy does not validate or time this protocol; its paths must not be mistaken
for adoption evidence.

### W42 — Localize synchronization and its indirect queue consequences

**Recorded:** [Locality](../orbital-scenarios/reconsideration/LOCALITY.md)
rejects importing a rare WAN wait into unrelated shard-wide completion.
**Generalization:** distinguish required dependency, reservation, shared barrier
and shared physical-resource delay. Continue independent work and measure drain
after the pause, not only its duration. Per-key queues alone do not prevent
V(y) waiting behind U(x,y) behind T(x) awaiting WAN; speculative future claims
can spread the convoy. **Evidence:** scenario locality probes exercise these
semantic distinctions; mixed demonstrates physical interference but no logical
dependency graph. A smaller aggregate message does not remove either form of
waiting by itself.

### W43 — Immutable generation publication saves coordination only under its contract

**Recorded:** [Read cases 14/16](../orbital-scenarios/reconsideration/READS.md)
and [worked ELT](../orbital-scenarios/ELT-WORKED.md) use historical products and
preserving generation updates.
**Generalization:** build source-qualified output privately, distribute it once
and publish small references when complete. Training, reports and authoritative
refreshes often fit. A stale root applying x=1 can erase a concurrent y=1;
atomic reference exchange does not preserve live edits. Readers must wait or
fetch missing committed objects, not substitute older ones. **Evidence:** finite
application histories check the distinction. Selection models missing base
acquisition, not multiobject generation activation or retained-reader GC.

### W44 — CDC is a moving frontier with collision and ownership semantics

**Recorded:** [Write cases 7–8](../orbital-scenarios/reconsideration/WRITES.md)
and [snapshot-plus-CDC example](../orbital-scenarios/ELT-WORKED.md) separate
trusted cut, ordered events, deduplication and activation.
**Generalization:** batch by source/transaction/key while preserving deletion
sequence tombstones, source order and checkpoint/output coupling. It can turn
repeated per-row work into a finite reproducible generation. A delayed older
event must not resurrect a deleted row; watermarks do not prove external
completeness or resolve independent-source/local-edit conflicts. **Evidence:**
small histories implement activation from supplied facts. Network scenarios do
not model snapshot/stream collision windows, connector gaps or multi-source
reconciliation.

### W45 — Index build, refresh and tenant hiding move work into later obligations

**Recorded:** [Write cases 2–3, 9–13](../orbital-scenarios/reconsideration/WRITES.md)
cover retention, erasure, online build and view maintenance.
**Generalization:** delay physical cleanup or build a new projection while old
readers continue, with explicit catch-up/activation and access-path rules.
This can bound foreground network work. A liveness record is ineffective if
one lookup bypasses it; it does not implement synchronous cascades or physical
erasure. Online indexes need capture/reconciliation, and always-current views
need pending corrections. **Evidence:** worked ELT covers finite subsets and
labels the rest reasoned. No current network case measures build/catch-up debt,
activation, cleanup starvation or all-path invalidation.

### W46 — Authorization, risk and exact rank are not just small outputs

**Recorded:** [Read cases 4–13 and 17](../orbital-scenarios/reconsideration/READS.md)
includes maxima, priority queues, rank awards, anti-joins, risk and authorization.
**Generalization:** use the semantic dependency to choose work/aggregation scope.
Historical maximum or any eligible job can be cheap; exact current rank,
absence or a global risk limit may remain broad despite a one-row answer.
Escrow/allowances or start-time authorization can simplify traffic only by an
explicit contract choice. Already exported bytes cannot be recalled on revoke.
**Evidence:** semantic histories include back-edge, phantom and invariant
counterexamples. Read/selection fixtures do not implement those updating or
continuous-authorization semantics.

### W47 — External interchange can be placed independently of storage layout

**Recorded:** [Shore](../../../shore/README.md) covers batch/stream formats,
connectors, export/restore and explicit lifetimes/backpressure.
**Generalization:** place format conversion at ingress, near retained data or
near the final sink based on reuse and edge cost; batch external calls where
their semantic boundary permits. A storage-native compressed projection may
save internal traffic yet need expansion to Arrow or another consumer format.
An oversized conversion buffer or slow client can retain upstream state and
block unrelated work. **Evidence:** this is provisional module scope; selection
prices selected raw rows, not external serialization, language bindings,
connector batching or real sink acknowledgments.

### W48 — Cancellation releases obligations at different times

**Recorded:** [Ikea integration](../../../ikea/docs/integration.md),
[Loom notebook](../../notebook/loom-objectives-and-architecture.md) and
[extension composition](../orbital-scenarios/reconsideration/COMPOSITION.md)
distinguish private work, pending I/O, published effects and external output.
**Generalization:** cancel future chunks, withdraw unused interest and release
owned state when backend activity actually ends; propagate identity-qualified
late completion handling. Already transmitted bytes and nonpreemptive work
are sunk costs. Cancellation after visible output is not rollback; shared
consumers and committed repair may remain owed. **Evidence:** mixed measures
large versus small chunk retirement after cancel; read suppresses some
not-yet-submitted hedge work. Neither models general backend abort completion,
client disconnect protocol or durable external effects.

### W49 — Representation-only relocation still needs a coherent cutover

**Recorded:** [Trie remapping](../trie-remapping/design.md),
[read/write catalog](../orbital-scenarios/reconsideration/WRITES.md) and
[Ikea integration](../../../ikea/docs/integration.md) separate logical identity,
mapping edition, visible data and resident addresses.
**Generalization:** move a shard, replica or physical region without inventing
logical row updates, and choose copy/replay/pause by workload. This can contain
SQL conflict scope but still spends network, temporary space and catch-up work.
Physical-location deltas cannot be replayed under a new mapping unqualified;
an old validated address can outlive neither its lease nor its generation.
**Evidence:** local remapping measures conversion without concurrency/WAL;
current dissemination has no mutable migration cutover or dual-layout replay.

### W50 — Model actors' knowledge and resource debt, not only final answers

**Recorded:** [Orbital simulation notebook](../../notebook/orbital-simulation.md)
separates observer knowledge, actor roles/hosts and admitted-history equivalence.
**Generalization:** expose the information required by a policy and instrument
outstanding work, retention, causal boundaries and failure outcomes independently.
A selector using future completion can produce an attractive but unimplementable
bound. Serially valid results need not prove identical deterministic fixpoints;
process kill is not a media power-loss test. **Evidence:** this spike labels
oracle selection and keeps unfinished cohorts; it is still a finite synthetic
resource model, not the proposed general protocol/disaster laboratory.

### W51 — Research and executable reuse also have amplification costs

**Recorded:** [Research experience](../../notebook/research-experience.md),
[build lessons](../../notebook/build-iteration.md) and
[layout recovery](../executable-placement/layout-recovery.md) distinguish
shared source/object reuse, captured experiments and selective retention.
**Generalization:** keep enough compiled/evidence context to change one operator
or link input without reconstructing everything; this is relevant to retained
plan/code deployment and to the research process. Sharing reduces duplicate
builds, but universal fine-grained interfaces can add runtime costs and retained
artifacts consume storage. **Evidence:** source-spike build/recovery observations
exist. No distributed code-cache deployment or remote compilation study is
included here; tooling conveniences are not network performance evidence.

## Coverage of the older workload catalogs

The full 21-read and 17-write case descriptions were inspected, not merely their
entry summaries. This mapping preserves discoverability without treating every
SQL example as a separately implemented network operator:

| Original cases | Relevant W rows |
| --- | --- |
| Reads 1 dashboard, 2 finance report, 3 historical maximum | W01, W02, W03 |
| Reads 4 maximum/action, 5 priority/eligible queue, 6 rank awards | W12, W46 |
| Reads 7 invoice joins, 8 anti-join, 9 dedup/fuzzy match | W04, W05, W40, W43, W46 |
| Reads 10 risk, 11 cross-row safety, 13 authorization | W12, W46, W48 |
| Reads 12 graph traversal, 18 pointer chasing | W01, W15, W17, W35, W40 |
| Reads 14 training, 15 CDC, 16 projection rebuild | W21, W37, W43, W44 |
| Reads 17 selection-driven update, 19 coarse protection | W39, W40, W42, W46 |
| Reads 20 long session/UDF, 21 changing plans/spill | W01, W28–W33, W37, W48 |
| Writes 1 price correction, 2 retention, 3 erasure | W39, W40, W45 |
| Writes 4 append, 5 INSERT SELECT, 6 MERGE | W39, W40, W43 |
| Writes 7 CDC corrections, 8 snapshot catch-up | W44 |
| Writes 9 backfill/index, 10 replacement, 11 refresh | W40, W43, W45 |
| Writes 12 aggregates, 16 blind/delta batch | W08–W12, W39, W41 |
| Writes 13 constraints/cascade, 14 payout | W36, W39, W40, W46, W48 |
| Writes 15 migration | W14, W16, W49 |
| Writes 17 RETURNING/triggers/audit/IDs | W12, W27, W36–W41, W48 |

These are analytical generalizations from the recorded scenarios. For example,
W46 does not establish an escrow design, and W44 does not implement all CDC
semantics. The old catalogs' rejected global lanes, common barriers and retry
alternatives remain discoverable in their owning prose; their local-cost
counterexamples are represented by W39–W43 rather than revived as recommendations.

## Inspected inventory and boundaries

The following prose was read in full for this contribution or its earlier
bounded contributions in this same task. A directory entry here names only
the exact files listed, not every file beneath it. Paths are relative to the
repository unless a local link states otherwise.

- Entry/ownership: README.md; workbench/README.md; workbench/notebook/ideas.md;
  workbench/spikes/README.md; loom/README.md; engine/README.md; shore/README.md;
  ikea/README.md; ikea/docs/integration.md.
- Notebook: workbench/notebook/secondary-summaries.md; prefix-addressing.md;
  pfor-bounds.md; loom-objectives-and-architecture.md; orbital-simulation.md;
  research-experience.md; build-iteration.md; calico.md. The consensus-networking
  note was read earlier and is owned by the parallel network survey.
- Every spike entry listed by workbench/spikes/README.md: ikea-composition,
  layout-analyser, tuple-layout, packed-integer-kernels,
  seriespack-range-execution, seriespack-head-projection, bec-packed-metadata,
  bec256-composition, executable-placement, memory-characterisation,
  cft-commit-latency, orbital-dissemination, orbital-scenarios,
  row-filter-signatures, aggregate-maintenance, trie-remapping and
  regexp-lowering — each directory's README.md.
- workbench/spikes/layout-analyser/: design.md; case-study.md.
- workbench/spikes/row-filter-signatures/: design.md; rollups.md; FINDINGS.md.
- workbench/spikes/aggregate-maintenance/: design.md; shared-dirty-buffer.md;
  FINDINGS.md; CONCLUSIONS.md; dirty-buffer/FINDINGS.md.
- workbench/spikes/trie-remapping/: design.md; FINDINGS.md.
- workbench/spikes/regexp-lowering/: CONCLUSIONS.md; FACTORED_FINDINGS.md;
  survivor-bounds.md; fsst-follow-up.md.
- workbench/spikes/ikea-composition/: design.md; value-reuse-sketches.md;
  semantics-and-integration.md; operation-granularity.md; sketches-2.md.
- workbench/spikes/bec256-composition/metadata-findings.md.
- workbench/spikes/seriespack-range-execution/: integration.md; ordinary-stores.md.
- workbench/spikes/seriespack-head-projection/findings.md.
- workbench/spikes/tuple-layout/: maintenance-review.md;
  batching/groups/README.md.
- workbench/spikes/memory-characterisation/FINDINGS.md.
- workbench/spikes/executable-placement/: layout-recovery.md;
  evidence/checked-point-layout-20260911/summary.md.
- ikea/docs/seriespack/extending.md; ikea/docs/tuplepack/extending.md;
  ikea/docs/bec256/usage.md.
- workbench/spikes/orbital-scenarios/: ELT-WORKED.md; CONVERGENCE.md;
  PIPELINING.md; reconsideration/README.md; reconsideration/READS.md;
  reconsideration/WRITES.md; reconsideration/COMPOSITION.md;
  reconsideration/LOCALITY.md.
- Current contribution/evidence descriptions:
  workbench/spikes/orbital-dissemination/READS-AND-EXTENSIONS.md; MIXED.md;
  ENHANCEMENT.md. Earlier implementation ownership/review covered read_probe.py,
  mixed_study.py and transport behavior; no prior source hash is claimed to
  identify the current whole repository.

Selected sections read, with the narrower boundary retained:

- ikea-composition/sketches.md: opening and “Clarification: analysis and execution
  surfaces,” through the small execution surface; sketches-2/design own the
  later full proposal.
- ikea-composition/native-regions/findings.md: first V2 screen, paired collector
  measurements, Zen/GNR grouped-read comparisons and reusable result curation.
  Other target chronology was not independently re-audited for this survey.
- memory-characterisation/METHOD.md: “Cores, caches and NUMA” and “Storage and
  transport,” including its explicitly unmeasured next distinctions.
- orbital-dissemination/read_scenario.py: module, run_reads and campaign setup;
  used to verify its cut/coverage, separate scan resource, cancellation and
  oracle-selector boundaries, not to claim a fresh campaign.

Boundary assignments: the parallel [network survey](SOURCE-SURVEY-NETWORK.md)
owns Orbital/stale drafts, MINING transport pointers, origin/routing algorithms,
sibling Calico transport/compression and host IPC. The parallel
[delivery survey](SOURCE-SURVEY-DELIVERY.md) owns recovery, terminal handoff,
identity/egress, SOS and PITR. Root owns the Consurgent pitch and current user
request inventory. References to those subjects here identify workload
consequences, not independent audits of all their source files.

Deliberate exclusions: individual instruction lowerings, every codec benchmark
run and ISA table, editor setup, full build recipes, artifact JSON/binaries,
external papers linked by the inspected prose, and the full sibling Calico
module tree were not recursively audited. Their source-spike guides retain the
details; this document does not turn README inspection into code verification.
The pure tooling notebook ideas are retained as W51 so they are discoverable,
while their process/build costs remain distinct from runtime dissemination.
No simulator source, retained result or owning module contract was changed by
this survey, and no tests or timings were rerun.
