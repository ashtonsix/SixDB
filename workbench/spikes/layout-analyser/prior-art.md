# Research dossier: the mechanisms behind the synthesis

Consolidated 2026-09-14. This is the supporting material for
[the design argument](design.md), organized by the questions the sources answer.
Current Ikea contracts describe what can be built today. Prototype reports and
retained captures describe particular experiments. The proposed connections and
extensions below are this spike's research hypotheses.

## The chain of reasoning

| Question | Material to use together | What the combination contributes |
| --- | --- | --- |
| What information can finish or avoid an operation? | Row-filter bounds and rollups; three-array's filter/core/cold programs | Sound conditional demand, including certainty, known-row bypass and joint evidence |
| How can that information be represented? | SeriesPack heads/residuals; TuplePack codes; three-array patches; predictive-PFOR dependencies | Movement, duplication and reconstruction are distinct transformations with different maintenance |
| How does representation become execution? | TuplePack maps/groups; Bec256 directory composition; current owner integration | Packets, address discovery, retained state, preserving effects and the actual consumer boundary |
| When does physical separation pay? | Filtering group occupancy; SeriesPack placement; spatial/MLP diagnostics and consumers | Logical omission must survive physical fetch, reconstruction and operating conditions |
| What can expensive analysis reuse? | PFOR's selected collection/container experiment; finite TuplePack search and palette replay | Transfer structural possibilities, fit local parameters, price complete candidates and retain coverage |
| How can advice improve while old bytes remain? | Resolved Ikea descriptors; grouped rebinding; trie-remapping; adaptation literature | Interpretation, execution choice and physical transition have different costs and lifetimes |

The sources do not jointly implement this process. They supply pieces strong
enough to define and test it. The gaps are now expressible as missing comparisons
and composition rules, rather than a generic need for more layout benchmarks.

## 1. Filtering supplies the semantics of omission

Start with [row-filter-signatures/design.md](../row-filter-signatures/design.md),
then [rollups.md](../row-filter-signatures/rollups.md), then its
[measured findings](../row-filter-signatures/FINDINGS.md). The design separates
feature, encoding, plane and stage. It distinguishes exact predicate bits,
necessary hash evidence, ordered prefixes/intervals, bins and combined signatures.
These are different abilities to reject, certify or refine a row, even at equal
bit budgets.

The lower/upper-bound account is reusable semantic machinery: L is certainly
true, U is possibly true, and U minus L needs refinement. Boolean composition
operates on those bounds. In particular, an exact OR branch can settle rows
whose other branches remain unknown. The rollup extension asks whether any
*same row* in a block can satisfy the expression. Replacing a joint summary with
marginal facts can lose that correlation; moving unchanged summaries between
planes cannot. OR-ing packed tag values is not a sound general
summary for equality.

This gives plane search more than a co-access heuristic. It supplies a way to
explain why a grouping permits omission and what evidence is lost by another
grouping. Actual head bits may already provide evidence; a separate dense copy
adds maintained information. Neither information budget nor row survival fixes
refinement group size or its nonempty-group distribution.

The measured counterexample matters: skipping 34.9% of second-stage 16-row groups
still cost 0.937 ns/row versus 0.684 for eager evaluation in one warmed local ARM
Linux VM case. Avoiding logical work can lose to the control and reconstruction
needed to avoid it. These are not measured DRAM-byte counts. This spike's later
Boolean model independently isolates clustering and partition effects, but has
no CPU timing. Together they justify keeping both logical-demand analysis and
complete consumer pricing.

## 2. Three-array connects evidence to exact records and mutations

Calico's [three-array report](../../../../calico/workbench/prototypes/three-array/REPORT.md)
is the main integrated predecessor. Read its monolithic controls, filter variants,
patch-rate experiments and mutation section together. Its three regions are a
4B filter, an exact 48/64B core, and cold scalar/patch/string payload. The core
contains values and metadata; the filter is not an obligatory first step for
known-row access.

Several controls separate mechanisms that the earlier synthesis blurred:

- Adding the same fingerprint information to a monolithic layout captures much
  of the selective-scan improvement. The independent compact filter is doing
  useful work; bit packing alone cannot take credit for that improvement.
- At 10% selectivity, the reported native scan took 2.002 ns/row while its filtered
  variant took 3.644; random probes favored filtering, 3.891 versus 9.039 ns/probe.
  The same evidence needs different programs under different access order.
- At one million rows, tri48 retained about 127.61B/row versus 174.97B for native96-fp,
  with essentially tied hot-point times around 25.5 ns. Full tri48 reconstruction
  took about 102.93 ns versus 79.97 for mono192-fp. Compact common access does not
  settle the full-projection choice.
- Widening eight common scalars from 20 to 24 bits consumed four existing padding
  bytes within the same 48B core. That experiment kept the patch threshold fixed;
  it did not measure the benefit of admitting more values without patches.

The cold arena connects read design to mutation. Rebuilding a patch can entail
rebuilding a payload containing other patches and a string. A split patch/string
alternative changes that dependency but was not implemented there. Joint patch
incidence over a projection determines cold demand; independent per-field rates
are only a model. The M1 Pro's 128B cache lines and synthetic distributions also
prevent using these timings as server coefficients.

The [point-layout report](../../../../calico/workbench/prototypes/point-layout/REPORT.md)
adds read/update clustering comparisons. Keep these sources in Calico; their
role here is to supply mechanisms and controls for the connected record case.

## 3. Ikea turns abstract planes into real representation and operation choices

### SeriesPack: information split, payload law and placement

The current [representation contract](../../../ikea/docs/seriespack/representation.md)
is the authority. K is the total width, and H can separate highest-byte heads
from the remaining payload. Local eight-row tiles and striped payloads impose
real residual geometry and final-tile occupancy. Independent origins and tile
strides allow different placements; a plane need not have its own allocation.
Recovery uses the resolved versioned descriptor, not today's preset policy.
Compound ownership identities and cross-child interpretation remain external.

Read [head projection](../seriespack-head-projection/README.md) and its
[reader experiment](../seriespack-head-projection/reader/README.md) for independently
accessing pieces. Then read the later
[placement study](../ikea-composition/placement/README.md). Its centered 12-bit
candidate rearranged `body64 + tail32` into `body32 + tail32 + body32`, improving
single-line incidence without changing the information. Tight 96B tiles improved
from 25% to 50% single-line incidence in that geometry. Consumer results did not
establish a broad win; the provisional wire change was withdrawn. Current v1
remains authoritative. Existing 10/20-bit formats already have centered structure.

These are three separate freedoms: choose which information is exposed as a
head, choose an admitted payload law, and place the resulting pieces. Better
fetch geometry is evidence for a candidate, not a reason by itself to change a
persistent format.

### TuplePack: stored layout and decoded arrangement have separate choices

Use the current [reference](../../../ikea/docs/tuplepack/reference.md),
[plan implementation](../../../ikea/include/ikea/tuplepack/plan.h),
[grouped example](../../../ikea/examples/tuplepack/groups.cpp) and
[owner integration guide](../../../ikea/docs/integration.md). A physical unit is
1..64B with byte-contained 1..8-bit codes. A decoded 8B/64B packet, row count,
map, group arrangement, physical extent and stride are different quantities.
Read maps can duplicate; write maps cannot. Holes and inactive original rows have
specified behavior. Preserving writes do not imply that issued bytes equal
logical destination bytes.

Packet groups make operation adaptation concrete. Four rows of A8/B4/C4 can
place their AB pairs together for a GPR computation, followed by C, and write AB
while preserving C in the original two-byte representation. The important
capability is storage-preserving composition of distinct read and write programs.

The [public grouping comparison](../tuple-layout/batching/groups/README.md)
qualifies cost. Earlier route-only folded experiments and later public operations
have different boundaries. The final public comparison includes native/ordinary
consumers, checked updates and several profiles; grouping was 11.1–22.7% slower
for its ordinary GPR read/consumer comparisons. It is therefore wrong to promote
an old routing win into a general grouping policy.

### Bec256 composition: metadata is evidence and address computation

The [metadata findings](../bec256-composition/metadata-findings.md) combine direct
and packed directories, absolute offsets and checkpoints, terminal population
facts, body traversal, native prefix state, and different consumer boundaries.
Read the retained-state and update qualifications with the footprint/timing tables.

Lengths locate subsequent bodies even when those preceding bodies are inactive.
A folded population of 255 needs length to distinguish 255 from 256. Empty/full
facts can eliminate body work and expose directory overhead. Checkpoint interval
changes fresh-entry work; retained prefix state changes scan work. Same-entry
metadata replacements do not price length-changing address/checkpoint repair.

This supplies a missing composition example: a child codec's bytes and accessor
are insufficient to price the parent's operation. Cross-child interpretation,
entry mode, retained state and repair must accompany them. The current Ikea
sources provide executable feasibility and owner obligations, not a generic
capability catalogue, calibrated cost oracle, search engine or publication policy.

## 4. Predictive-PFOR contributes two different mechanisms

The earlier [design](../../../../calico/workbench/prototypes/predictive-pfor/DESIGN.md)
sets up collection/container reasoning. The later
[selected specification](../../../../calico/workbench/prototypes/predictive-pfor/selection/SPEC.md)
and [report](../../../../calico/workbench/prototypes/predictive-pfor/selection/REPORT.md)
are the authority for that experiment's chosen algorithm. They should not be
blended with the earlier six-attempt, slope-seed or deeper-graph proposals.

First, its representation establishes reconstruction and maintenance dependencies:
a predicted field uses residuals and one or two raw references. The selected
numeric design has no reference chains and restricts fanout. A point projection
of that field can require several blocks, whereas a full projection can share
references. Changing a source can require dependent re-encoding. A compression
benefit is consequently one component of a layout/operation decision.

Second, historical learning ranks field/reference/form hypotheses by observed
local usefulness. Parameters are fitted anew. Up to four attempts use bounded
training/validation, then exact full-container codec pricing including metadata
controls acceptance. Untested candidates are not failures. Pooled correlation
can mislead because useful local parameters and container means vary.

The transferable idea is reusable structural hypotheses with bounded, declineable
local fit. The exact byte acceptance rule does not become an exact runtime
acceptance rule: future workload cost remains estimated. Independent historical
containers, acquisition cost, reference baselines and compatibility must survive
into our experiment. The full-menu greedy comparison is a finite reference,
not a proof of global optimality.

## 5. Hardware, search and history supply different kinds of evidence

[Memory-characterisation](../memory-characterisation/README.md), its
[method](../memory-characterisation/METHOD.md),
[findings](../memory-characterisation/FINDINGS.md) and
[lookup](../memory-characterisation/lookup.json) own hardware diagnosis. Spatial
samples time a particular target after a particular access history/delay. Random
independent chains and ordered streams ask different questions. Their thresholds
do not directly reveal queue sizes; normalized spatial savings are not hit
probabilities. Loom retains this interpretation; derived consumers belong here.

Our [first findings](findings.md) provide conditional comparisons of prefix
boundaries, bucket choices and 96/128B placement across request concurrency. They
show why complete use changes a geometry verdict. They do not identify spatial
prefetch causally, and their arithmetic-address extension is not a variable tail
with a missed locator. This distinction is now explicit in the worked case.

The transferred [TuplePack reference](tuplepack-reference/README.md) and
[search note](tuplepack-search.md) are owned here; runtime evidence stays in
[tuple-layout](../tuple-layout/README.md). The reference enumerates 2,816 legal
placements for widths `[1,7,3,5]`, seven ordered read/write operations and a diverse
28-layout subset. Twelve checks establish that finite search/accounting example,
not a general predictor.

Historical mixed captures directly challenge isolated-cost selection. In
[mixed-v2](../tuple-layout/evidence/mixed-v2/provenance.json), the isolated-cost
choice had about 65% regret at 50%/90% writes over 56 measured complete plans.
A later [final-v2 capture](../tuple-layout/evidence/final-v2/provenance.json)
had 0.09%, 44.50% and 14.09% subset regret at 5%/50%/90% writes. These are distinct
captures, not repeat measurements to pool. Today's offline palette replay shows
shortlist/local-pricing promise within retained captures; it has not tested
independent-container or future-window generalization.

[Trie-remapping](../trie-remapping/FINDINGS.md) separates stable identity from
physical payload order. Stable labels reduced reference repair while a packed-rank
column could still move. Gaps had to reach payload placement to remove those
shifts in its resident model. This links layout to historical adaptation and
rollup coverage: movement can require physical repair without a logical value
change. Publication, retained readers and durability were outside that model.

## 6. Literature: mechanisms to import, assumptions to test

These primary sources extend the local account. Each proposed use is an inference
for this spike, not a claim by the authors about SixDB.

| Source and reading focus | Contribution to this investigation | What it leaves for us |
| --- | --- | --- |
| [HYRISE — Grund et al., 2010](https://www.vldb.org/pvldb/vol4/p105-grund.pdf), layout manager and cache-cost sections | Workload-driven attribute containers and geometry-aware layout search connect demand to partitioning | Its fixed-width/container and operator assumptions do not supply costs for arbitrary reconstruction, ordered packets, shared effects or mixed consumers |
| [PAX — Ailamaki et al., 2001](https://www.vldb.org/conf/2001/P169.pdf), §3–4 | Attribute minipages retain page-local reconstruction while changing cache locality; partitioning can recur inside a larger unit | We must choose our own storage, refinement and execution grains; a page boundary is not a SIMD or scheduling boundary |
| [H2O — Alagiannis et al., 2014](https://stratos.seas.harvard.edu/sites/g/files/omnuum4611/files/stratos/files/h2o.pdf), joint layout/access adaptation | Representation and access adaptation, heterogeneous data parts and transformation costs belong in one design problem | Separate cheap rebinding from byte conversion and price the actual SixDB ownership/lifetime costs |
| [ByteStore — Zhang et al., 2022](https://arxiv.org/abs/2209.00220) | Byte-oriented hybrid layouts and scan-oriented layout advice connect representation alternatives to their consumers | Extend beyond its scan setting to conditional reconstruction, writes and compound metadata; do not use scan profiles as a universal objective |
| [Workload as a Sequence — Agrawal, Chu and Narasayya, 2006](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/SequenceTuning_Sig06.pdf) | Physical design can depend on ordered query/update phases and transition costs, not just aggregate counts | Supply authored horizons now; production workload forecasting and Engine transition costs remain open |
| [Scalable Exploration of Physical Database Design — König and Nabar, 2006](https://www.microsoft.com/en-us/research/publication/scalable-exploration-of-physical-database-design/) | Its abstract describes comparing configurations from a sampled workload with conservative treatment of skew | A precedent for spending evaluation effort on consequential comparisons; neither its confidence machinery nor a runtime predictor is implemented here |
| [Database Cracking — Idreos, Kersten and Manegold, 2007](https://www.cidrdb.org/cidr2007/papers/cidr07p07.pdf) | Query-driven incremental physical reorganization offers an alternative to eagerly converting all data | Our palette/new-data-only/rebinding choices are different actions; compare their costs rather than assuming adaptation requires cracking |
| [Cranelift e-graph RFC — Bytecode Alliance, 2022](https://github.com/bytecodealliance/rfcs/blob/main/accepted/cranelift-egraph.md) | Retained equivalence permits cooperating rewrites; extraction must also deal with scheduling | We need explicit reconstruction/effect preconditions and shared/contextual costs before a general layout e-graph is justified |
| [Swiss Tables — Abseil](https://abseil.io/about/design/swisstables) | Compact metadata filters possible key matches before exact equality | The seven-bit H2/control organization is not our variable-N rope-bucket design; it motivates a dependency, not a transplanted format |

The literature does not remove the need for local synthesis. PAX and HYRISE
explain forms of physical grouping; filtering adds sound conditional omission;
Ikea exposes real executable compositions; H2O and workload sequences bring
adaptation and transitions into the objective; predictive-PFOR supplies a concrete
local precedent for amortizing discovery. Their intersection motivates the
joint representation/program family in the design.

## What is assembled, and what still has to be established

We now have sources for evidence semantics, exact reconstruction, real codec
geometry, packet/consumer choices, address dependencies, maintenance examples,
conditional hardware observations, and reusable fitting. The connected case
makes each participate in a decision rather than treating each as an isolated
optimization topic.

What remains is empirical and algorithmic: a bounded search over those connected
choices; an independent-container test of reusable families; and a demonstration
of useful adaptation with retained images. There is no claim yet of a general
semantic checker, transferable CPU cost model, learned region partitioner or
online migration implementation. The [next experiments](experiments.md) target
those distinctions without requiring an Engine module contract.
