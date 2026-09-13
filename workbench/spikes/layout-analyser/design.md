# Candidate meaning, costs and reuse

Proposal, 2026-09-13. The useful separation is between **what alternatives mean**,
**whether a primitive can execute them**, and **what they cost in a context**.
These are different claims, not a required sequence of implementation stages.

## Three independent axes

| Axis | Choices / questions |
| --- | --- |
| Scale | Collection/index/workload choices; local segment, bucket, tile, record or field-fragment choices |
| Evidence and decision lifetime | Expensive training and hypothesis discovery; fast selection; bounded fitting; execution binding; optional migration |
| Candidate dimensions | Information and dependency structure; plane partitioning; encoding; unit boundaries and placement; operation recipe and schedule |

A **plane** is an independently addressable stream of information. A field can
contribute to several planes; a plane can combine several fields. Physical
separation is useful when an operation can omit or delay its loads, but adjacency
and hardware fetch behavior can still couple traffic. A plane is not inherently
a separate allocation, page, persistence object, SIMD vector or cache line.
Information budgets also differ from allocation: splitting four bytes into four
one-byte planes saves no payload space when all four must be stored and read.

Macro may propose a signature, exact base record and exception stream. Micro may
move one prefix byte, group evidence by Boolean clause, change a tile or divide
one coarse plane. The boundary should express which choices remain open, rather
than silently treating macro's first grouping as fixed. Micro can return a
trade-off frontier or a counterexample requiring another macro alternative.

## What a data-unit description needs

Start with explicit fixtures and only generalize repeated needs. A useful
experimental input contains:

- Logical fields/fragments, types and admitted widths; exact reconstruction;
  variable-length distributions; NULL, presence, patch and exception semantics.
  State whether an inline prefix length is preferred or required, how string
  length/order/collation is represented, and which fragments may move or duplicate.
- Required operations, ordered projections and write maps, Boolean expressions,
  exact versus necessary evidence, consumer result, and dependencies needed to
  obtain it. Include negative lookups, inactive rows and known-row bypasses.
- Coordinate domains and mappings: keys, segment-local positions, occupied ranks,
  tuple indices and physical locations. The current at-most-`2^16` segment-local
  position limit is neither a byte limit nor a requirement for densely packed rows.
- Admitted storage/operation capabilities: candidate codecs, unit/code limits,
  independent plane origins, allowed strides, alignment, whole-tile occupation,
  preserving writes, and maintenance dependencies. Restrictions can be hard
  requirements or gaps to take back to the primitive owner.
- Tunable capacity and slack: bucket occupancy, fingerprint width, string prefix,
  presence organization, tile count, per-plane and inter-unit padding. Metadata,
  offsets, partial final tiles and allocator constraints count toward footprint.

TuplePack currently has a 1..64B physical unit, with individual 1..8-bit codes
contained in bytes. Wider logical fields decompose into codes, and wider records
compose units. Its 8B/64B decoded packet, number of rows, occupied extent and row
stride are separate dimensions. SeriesPack's logical count does not remove the
storage occupied by a partial final tile. Use their actual contracts, not an
idealized bit-packing lower bound, when declaring a candidate feasible.

An output candidate describes its information dependencies, resolved per-plane
representation, occupied bytes and placement constraints, plus operation recipes
and their prerequisites. A selected candidate promises valid interpretation and
operations under those prerequisites; it promises no universal performance rank.
Keep model estimates, measured costs and unmeasured terms distinguishable.

## Preserve alternatives across scales

An equivalence graph is one possible way to avoid committing early: a node can
represent the same logical result obtained from an inline prefix and tail, a
separate prefix plane and tail, or a base plus sparse exceptions. Rules must state
their preconditions, exact reconstruction and update/summary obligations. A
micro-index adds maintained information; equivalence is at the requested logical
behavior, not equality of stored bytes or maintenance work.

Cranelift supplies the precedent of retaining equivalent expressions and
extracting a costed implementation; [the literature](prior-art.md#external-mechanisms)
does not establish a layout algebra for us. Initially use an explicit list or
small DAG of alternatives. Test whether rewrite order actually hides useful
layouts before building a general e-graph. Shared planes, shared preparation and
conditional reads make extraction context dependent; summing independent child
costs can double-charge shared work or miss interactions. Keep an exhaustive
small reference and report larger searches as best-found, not globally optimal.

## Define and consume an objective

The macro-facing seam can supply a complete objective or several scenarios:
minimize expected complete-operation time subject to a space budget; minimize
storage subject to point-read and update limits; or return a Pareto frontier.
Relative field co-location value may be conditional on a Boolean fragment or
other requested fields. A pairwise affinity matrix is a candidate-generation
hint and can lose higher-order relationships and ordered-map costs.

Provisional vocabulary, not a serialized API:

```text
problem = semantics + admitted choices + workload scenarios + hard constraints
context = hardware evidence + placement/access conditions + execution identity
assess(candidate, scenario, context) -> costs + applicability + provenance/unknowns
compare(costs, horizon, resource limits) -> verdict or unresolved trade-off
```

Keep these cost terms independently inspectable:

| Term | Scope and unit to declare |
| --- | --- |
| Training / calibration | Total CPU or elapsed time and evidence bytes for the reused collection/region study |
| Local selection / fit | Time, rows inspected and candidates attempted for this new unit |
| Preparation | Actual schema/code and operation binding events, retained for a specified lifetime |
| Execution | Complete lookup/update/scan result, in ns per stated operation or throughput; conditional/tail behavior where relevant |
| Space | Logical payload, allocated bytes, descriptors, index/filter overhead, scratch and peak overlapping images |
| Transition | Conversion and repair work, new preparation, publication/reclamation and foreground interference where supplied |

For an explicitly additive *accounting* scenario, one may write
`J = training_share + local_fit + preparation_events + transition + execution`.
Each event must be charged exactly once. A space price requires a named unit
(for example ns-equivalent per byte over this horizon) or an explicit constraint;
do not add raw bytes and nanoseconds. A percentile constraint must be evaluated
from an appropriate distribution, not a weighted average of per-operation p99s.

Execution can be a measured whole mixed trace or a conditional model. It must
not silently default to the sum of isolated kernel timings: retained TuplePack
mixtures falsify that as a final chooser. Independent invocation weighting is
useful only where the context supports it. Missing observations mean unknown,
not free. An estimate can remain an explicitly provisional candidate score;
it is not an optimistic zero to fill a missing measured table entry.

Useful nonlinear features include line-count distributions and start phases,
critical dependency depth, cache-capacity and TLB thresholds, request concurrency,
useful work before a dependent plane, prefetch/lookahead, write preservation,
shared-byte ownership, conditional group demand and reconstruction cost. Fixed
penalties for “split loads” or a universal power of line count are hypotheses.
Actual load/store spans may include preserved neighbors; logical selected bytes,
issued spans, cache-line demand and measured memory traffic are different metrics.

Useful starting heuristics should follow the consumer's limiting work:

| Consumer regime | Candidate preference to test |
| --- | --- |
| Serial point lookup | Reduce exposed dependent misses and address-discovery steps; compare complete dependency chains, not line count alone |
| Many independent lookups | Measure throughput as request concurrency changes; an additional line may overlap while extra bytes still consume resources |
| Scan / selective projection | Reduce total unique lines and reconstruction work over the requested rows; account for shared lines and nonempty refinement groups |
| Capacity-sensitive working set | Keep padding and replicated metadata visible; crossing a measured cache/TLB working-set boundary can reverse a local alignment win |
| Partial updates / concurrent writers | Compare preserving loads/stores, ownership transfer and false sharing as well as useful write bytes; keep publication costs explicit |

These are reasons to retain competing candidates and define scenario controls,
not architecture-independent scoring coefficients. A dense 64B unit is a useful
candidate, not a privileged final answer.

Hardware evidence distinguishes reported geometry, conditional observations and
consumer policy. Retain source/capture identity, CPU and software context,
affinity/sharing conditions, actual memory placement, bases/strides/phases,
load order/width, dependencies, reuse, batch/concurrency and conditioning.
Spatial-fetch normalized latency savings are **not** hit probabilities or
whole-record discounts. Extra fetched lines can hide latency while still costing
bandwidth. A “validated” lookup entry does not validate every adjacency field.

## Training, selection and bounded fitting

One practical hypothesis is a reusable palette of layouts, with fast selection
for a new segment/bucket class. Another is a palette of structural hypotheses
from which a bounded local fit adjusts prefix lengths, widths, capacity or plane
membership. Compare both with a fixed baseline and a small exhaustive reference.
Neither requires expensive analysis for every inserted record.

The [predictive-PFOR prior](prior-art.md#predictive-pfor-training-is-not-selection)
offers the key distinction: discover promising relationships across independently
fitted containers, then fit parameters locally. Pooled row correlation can be
misleading. Likewise, learn where a layout hypothesis wins without assuming one
parameter choice or machine-specific ranking applies everywhere.

Candidate experiments should separate:

1. **Train:** sample immutable containers and workload windows; discover diverse
   hypotheses and retain successes, failures, applicability and sampling identity.
   Untested alternatives are not negative observations.
2. **Select / fit:** use compatible evidence plus cheap current statistics to
   choose a palette member or spend a bounded search budget. Keep a valid baseline
   and allow a fit to decline. Exact footprint/validity checks can be cheap even
   when execution cost remains an estimate.
3. **Observe / revise:** collect outcomes and coverage, evaluate held-out regions
   and later windows, and produce a new advice revision. Log acquisition and
   fitting cost separately so expensive discovery does not disappear from results.

These name work and evidence, not one mandatory linear pipeline. A new machine
may preserve semantic alternatives and representation validity but invalidate
rankings. A workload change may preserve codec timing evidence while changing
its applicability or objective. A schema/algorithm change can invalidate both.
Keep distinct compatibility keys for semantic hypotheses, cost observations,
palettes, execution bindings and stored descriptors.

Collection-wide advice may miss index-key-dependent sparsity, lengths, hot fields
or access distributions. Compare collection, key-region and local advice with
known region boundaries before inventing a learned partitioner. Retain sample
coverage and cold/unseen-region behavior. Cheap exploration or scheduled audits
may prevent a stale palette hiding new opportunities; automatic drift detection,
forgetting, budget policy and boundary creation remain experiments.

## Advice does not rewrite history

Separate logical schema revision, coordinate-map edition, actual representation
revision, visible data version and analysis/advice revision. Stored bytes must be
interpretable without rerunning today's policy. Actual SeriesPack descriptors
already provide a useful precedent; Engine must additionally retain identities,
offsets, compound dependencies and the applicable semantic description.

Keep four actions available in cost scenarios: retain the current layout and
binding; improve its binding; select a layout for newly encoded data; or migrate
an existing unit. Historical segments may retain different layouts indefinitely.
Selection from a trained palette is not authorization or an obligation to migrate.
Group execution by compatible layouts only when reordering preserves semantics
and pays for dispatch/gather work; retain arbitrary mixed-layout controls.

A relocation counterexample to carry through experiments: one segment changes
its base/optional/patch arrangement while an old reader retains the previous
mapping, and another segment keeps its old representation. Name which descriptor
interprets each image and what invalidates a bound operation. Presence, SQL NULL,
inactive selection and physically omitted payload remain different states.
Physical movement alone creates no logical aggregate delta, but can change
reference repairs or summary coverage. Exact/conservative/bypass obligations
come from the owner contract, not an assumed future transaction mechanism.

Migration accounting includes admitted conversion reads/writes, locator/index/
summary repair, overlapping source and destination while readers retain data,
scratch, new preparation and supplied publication/reclamation costs. Durability,
replication, page COW and interference are explicit unknowns unless the owner
supplies a scenario. Issued byte coverage cannot determine page or network costs.
Resident preparation is sunk. Break-even against stable future rates is a
conditional calculation, not a forecast of reuse. Investigate robust horizons
and hysteresis only after there is evidence of ranking noise or repeated churn.
