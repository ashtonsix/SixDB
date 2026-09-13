# Hypotheses and experiments

Proposed work, 2026-09-13. Begin with exact small comparisons and ordinary
consumers. These are useful next questions, not a mandatory roadmap or a new
benchmark framework. No hardware campaign was run to open this spike.

## Cost consumption and small search

**Hypothesis:** a diverse shortlist generated from structural features and
isolated costs can find useful complete plans at much lower analysis cost than
exhaustive consumer measurement. A density or co-access winner alone is brittle.

Start from the relocated [TuplePack reference](tuplepack-reference/README.md):
2,816 layouts of four codes, seven ordered read/write operations, preparation,
recipe choice and conditional migration arithmetic. Replay its synthetic checks
and the retained mixed-plan counterexample before extending the input vocabulary.
Its four-code universe and cost-table schema are controls, not the proposed
general module. Do not copy its fixed features into a purported hardware model.

Compare density, bit-edge, co-access, diverse sampling and cost-ranked shortlists
at equal candidate/measurement budgets. Price finalists with complete mixed
traces, including actual consumer, validation and effects. Use held-out mixes
and orderings; allow arbitrary per-operation recipes only when actually measured.
Report regret against the same finite measured universe, shortlist coverage,
selection/prepare time and candidate count. Include omitted winners and ties,
not just the selected layouts. The hypothesis loses value if a cheap fixed
palette matches it or if discovery consumes more than the plausible reuse saves.

Exercise objective consumption with authored scenarios: read-heavy, balanced,
write-heavy; cold/new binding versus retained binding; minimum time under a space
cap; and a space/time frontier. Validate incompatible context, missing costs,
unit mismatch and no-win/short-horizon cases. A non-additive complete-trace input
should be representable without pretending to be isolated operation coefficients.
Cost-function production from real Engine telemetry remains outside this initial
experiment.

## Boundary relocation and plane omission

**Hypothesis:** moving useful information across a 63/64/65B boundary can beat
padding, and the best move depends on conditional access and update cost.

Use the two equivalent-record families in [the example](examples.md#a-string-prefix-near-the-64b-boundary).
Enumerate inline prefix lengths around eight, moved optional/patch bytes,
separate prefix/presence planes and explicit padding. Count exact occupied bytes,
including arena rounding and directories. Keep result semantics and all values
fixed within a family. Include a scalar byte-array control and admitted ordinary
SeriesPack/TuplePack compositions; report capability gaps rather than quietly
weakening a requested operation.

Vary prefix collision/length distributions, projection mix and patch correlation.
Include known-row reads, prefix filtering with exact refinement, full strings and
preserving partial writes. Price the extra byte in a nine-byte comparison and
the dependency created by shortening a prefix. Establish hot-working-set controls
before larger placements. Keep row count fixed for the main comparison, with
matched query IDs and outputs. Reject the hypothesis for any region where an
extra plane/prefix step costs more than the saved demand or space.

**Related hypothesis:** Boolean grouping and refinement demand predict the value
of a plane better than aggregate row survival or pairwise field affinity.

Reuse equal-information 1×32, 2×16 and 4×8 signature layouts, plus interleaved and
independent placements. Include `(A OR C) AND (B OR D)` and `(A AND B) OR C`,
exact versus necessary evidence, cheap exact predicates and expensive strings.
Track certain matches, possible matches and the rows still requiring refinement.
For independent unresolved rows with probability p, a g-row group is visited
with probability `1-(1-p)^g`; at p=1/256 this is about 6%, 22%, 63% for g=16,64,256.
Compare IID and clustered unresolved rows at the same p. Count real stream loads,
logical bytes and measured time separately. Saving logical bytes while becoming
slower is a useful falsifier already present in the prior art.

## Spatial placement and consumer timing

**Hypothesis:** target-specific spatial fetch changes the split/dense/padded
verdict in some co-access and timing regions, so a palette should retain those
alternatives rather than baking in a universal line penalty.

Loom's proposed bounded comparison uses the same 64B core + 32B extension in
[three organizations](examples.md#same-96b-record-separate-dense-or-padded).
Keep logical row count, IDs, values, extension decisions and consumed results
fixed. Report core-only and conditional/full-extension paths as well as their
mixture. Footprint is an intended consequence; an equal-byte diagnostic is an
additional attribution control, not the primary comparison.

Start with one pinned idle core and an observed memory placement. Compare serial
dependent requests and a small independent-request set such as K=1/8/32, charging
the whole loop/context. Use a hot-working-set control for arithmetic/decoding,
then a larger set qualified by exposed cache geometry and measured controls.
Do not call an arbitrary 256MiB allocation pure DRAM. Balance base phases, retain
actual offsets/page crossings, and randomize logical request order. Pair or
randomize candidate order within repetitions; use independent seeds.

Around a close decision, vary identical **useful computation** between core and
extension and explicit lookahead, then add ordered requests. Consume the computed
result. Empty barriers are not a calibrated processing delay. Put extra fences
and timestamps in a separate latency diagnostic, not inside every consumer
record iteration. A Zen 5 and Granite Rapids comparison is motivated by the
existing diagnostics; Granite Rapids is not a “no-prefetch” control. Repetition
on one worker does not establish fleet-wide variation.

Report ns per completed operation, allocated/useful bytes, conditional extension
counts, and request concurrency. Model line demand separately from measured
traffic. Do not independently add prefetch savings, MLP overlap and compute
costs. A reversal with co-access, K, delay or placement is evidence for preserving
that contextual distinction. If there is no consumer benefit despite a positive
adjacency diagnostic, revise the layout policy rather than declaring the
diagnostic a whole-record prediction. Hardware interpretation remains with
[memory-characterisation](../memory-characterisation/README.md).

## Bucket capacity and fingerprint budget

**Hypothesis:** jointly choosing N, fingerprint width and padding beats choosing
a bucket byte size first and treating the remainder as waste.

Start with the exact [8/9/16-entry fixture](examples.md#a-hash-bucket-capacity-fingerprints-and-slack),
then vary key/value widths, metadata budgets and allowed SeriesPack formats.
Enumerate actual tile occupation, TuplePack code maps, region offsets and bucket
stride. Compare the current tuple key/value arrangement with separated keys and
values. Keep an explicit scalar/unpacked control and a fixed-capacity baseline.

For a fixed collection key count, vary specified load factor, skew, key entropy,
success/miss ratio, value projection and update mix. Measure occupancy and rope
length rather than inferring them from the false-fingerprint formula. Charge
hashing, fingerprint scans, exact keys, values, metadata, overflow, construction
and updates. A lower false-candidate count is not a speed win by itself. Widening
a fingerprint can change SIMD/decode work and the number of useful entries per
primitive; N can change both dependency depth and footprint.

The bounded reference is the best measured admitted `(N,b,placement,recipe)` in
the declared range. Compare larger searches by equal work budget and disclose
unexplored candidates. Any estimated occupancy distribution remains labeled a
model until checked against the generated/observed table.

## Palette reuse, local fitting and drift

**Hypothesis:** a small, diverse palette or bounded fit from trained hypotheses
keeps most of the measured benefit while amortizing expensive analysis across
many units, including units in distinct index-key regions.

Use separate immutable containers and workload windows for acquisition, selector
tuning and evaluation. Test a stationary collection, opposing known key regions,
a shifted later window and an unseen region. Never split random rows from one
container and call them independent historical containers. Freeze training
evidence for each evaluation; retain sample/region identities and selection
coverage. Include distributions with correlated pooled means but different local
behavior, following predictive-PFOR's counterexample.

Compare a single fixed layout; one collection-wide trained choice; a palette of
sizes 2/4/8 selected from cheap statistics; bounded local fitting from those
hypotheses; and the small exhaustive reference. The palette sizes are experimental
budgets, not chosen product knobs. Report total training/selection/fit work,
held-out regret, descriptor and binding footprint, fallback rate and sensitivity
to stale or incompatible advice. Compare future evidence refresh with keeping
the old palette; do not assume automatic decay or slope/parameter transfer works.

## Historical layouts and transition decisions

**Hypothesis:** selecting layouts for new units and improving existing bindings
captures useful benefit even when most historical units never migrate.

Build a model with two regions using different layouts, an old retained reader,
and a changed recommendation. Decode both images from actual descriptors after
changing the advice. Distinguish a new binding, new encoding and physical
migration. Include the coordinate-map/optional/patch counterexample in
[the design](design.md#advice-does-not-rewrite-history).

Compare stay, rebind, new-data-only and migration for explicit reuse horizons
and supplied transition costs. Price source/destination overlap and required
repairs. Give unimplemented publication/durability/interference terms named
unknowns or authored values, not measured labels. Check zero/negative advantage,
sunk preparation, recipe changes and ties. A favorable modeled break-even does
not prove practical online migration until the owner costs exist.

## Evidence to retain

Use [existing measurement conventions](../../benchmarks/README.md),
[tools](../../tools/README.md) and [artifact retention](../../tools/artifacts.md).
Keep enough candidate definitions, workload/split identities, outputs and
rejections to reproduce a ranking and its regret. Preserve source and executable
identity, controls and limitations. Reuse captured runtime evidence in place;
derived outputs can live in ignored `build/layout-analyser/`. No fresh cloud
fleet or generic search framework is necessary to begin.
