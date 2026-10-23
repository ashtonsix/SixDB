# Frame stocktake and research map

Joint Ikea/Engine/Claude stocktake, 23 October 2026, approximately twelve hours
into the spike. This records the evidence and the decisions it can inform;
it does not select a Frame interface, defaults or graduation scope.

## Where we are against the brief

We have a substantial integer encoding and execution laboratory, several strong
examples of avoiding work, and useful executable ownership laws. We do **not**
yet have a generally competitive Frame, or coverage approaching the original
operation-family ambition. SUM/COUNT, points and replacement histories account
for most of the investigation. TuplePack field groups and floating-point data
are particularly large holes.

The [original bar](README.md#how-this-spike-works)—at least three examined
operations per family at 2× a strong system baseline, with Calico parity—remains
unmet. Some individual operations exceed it substantially. Neither those wins
nor the correctness counts establish a whole family. Calico has supplied useful
[source-based prior art](SURVEY.md), not a systematic same-work parity result.

Three findings change the starting point for the rest of the work:

1. **Structure can replace computation.** Runs, monotone predictions, selected
   algebra and some shared codes remove row work. These are the strongest wins.
2. **Smaller bytes do not choose the program.** Native narrow/FOR arrays beat
   many current compressed consumers, including with flushed inputs. A larger
   zero-filled image can beat the minimum-byte predicted image through writes.
3. **Planning composition and execution fusion are compatible.** Engine can
   understand a representation's useful relations while executing a complete,
   fused Ikea operation. Rich preparation has not generally beaten a strong
   opaque operation using the same algorithm. Its purpose must be finding a
   better algorithm or avoiding work, rather than exposing every codec step.

## Evidence inventory

“Strong” below means a useful result under its named conditions. It does not
mean a default across CPUs, populations or operations. Detailed evidence and
counterexamples stay with the linked owner.

| Area | Strong or established | Weakness or remaining boundary |
| --- | --- | --- |
| Integer representation | Actual constant, runs, packed FOR, patched, delta and dictionary bodies. Real row order changes eleven columns from 179 to 294 bits/row when shuffled; reclustering gives 238. Arrival dictionaries bring URLHash to 43.9 bits/value versus sorted 50.9 and ZSTD-3 41.4. | Layout and order are consequential choices, not a universal pipeline. Whole-corpus DuckDB/Parquet comparisons include **models**, not all real bodies. Complete encode/conversion throughput against strong writers is unqualified. [Encoding findings](encoding/README.md). |
| Algebraic reads | Zen 5 clustered UserID COUNT takes about 42 µs repeated / 52 µs flushed, versus compact native 460 / 542 µs. Weighted delta SUM avoids prefix reconstruction; monotone COUNT can search. | EventTime flushed COUNT is near compact-native/compact-Polars parity. OS and ResolutionWidth full SUM lose badly: about 483 versus 81 µs, and 552 versus 149 µs, respectively, with flushed inputs. [Controlled scans](scan/FINDINGS.md). |
| Selection and NULL laws | Coarse selection can skip descriptor/body work; empty fine selections need no payload. Selected runs, packed planes, patches and weighted deltas have independently checked laws. Original coordinates, presence and validity remain separate. | Sparse output does not imply sparse input dependencies. This is substantial SUM/COUNT coverage, not the all-family mask requirement. Physical NULL compaction/routing remains a byte model. [Selected operations](encoding/README.md#row-selected-aggregates-and-null-fill). |
| Selective movement | UserID supplied-mask SUM over the same contiguous 1/16 selection takes 23.3 µs flushed versus native 40.5; repeated work is near parity. Frame beats actual Polars consumption in these cells. | Scatter the **same selected values**, with equal encoded image bytes, and flushed Frame takes 131.6 versus native 51.1 µs. Predicate(A) → consume(B), dependent-load staging and larger shared-bus tests are still missing. [Scan findings](scan/FINDINGS.md). |
| Query evidence | Logical H16 planes help complete Jena COUNT queries. Small exact block bounds are useful and cheaper; residual heads do not automatically have logical-value meaning. | H16 adds two bytes/row and can lose. RegionID rules out universal heads; current q8 window bounds lose. Evidence maintenance through writes is unpriced. The [timing-order audit](ORDER-AUDIT.md) supersedes close historical rankings. |
| Points | Native run-source access reduces the local run-heavy point cost by roughly 33–38%; in-place delta prefix sums take 0.48–0.73× the previous time on real predicted columns. | Native arrays remain substantially faster. These improvements do not establish cold point throughput or Loom staging. Checkpoint bytes/latency and representation choice remain real tradeoffs. [Point work](point/README.md), [native prefix](encoding/README.md#delta-point-reads-through-the-catalogue-sum). |
| Dictionary preparation | Per-identity retained material makes repeated equality and sparse COUNT much faster in local tests. Sorted, arrival and projected dictionaries have distinct useful laws. | The full cache can approach twice its column's bytes; first preparation is expensive, SUM barely benefits, and points get slower. A reusable preparation boundary is valuable; universal caching is not. [Caches](encoding/README.md#dictionary-caches-across-calls). |
| Cross-column structure | OS grouping over shared codes takes 73.9 µs versus the best native shared-code control's 148.2 µs on local ARM. Projected forward maps need not be injective; local reductions can precede logical translation and actual hash output. | Isolated OS SUM becomes near parity with proper native lane widths. Region and functional shared forms beat independent encoding but lose stronger native complete controls. Decorrelation loses runtime and bytes against independent encoding too. Shared codes are one tested relationship, not coverage of routing, conditioned predictors or TuplePack. [Domain-sized controls](execution/NATIVE-DOMAINS.md). |
| Mutation | Actual fitting packed/delta repair, byte damage, overrides, block folds, thaw/reseal, NULL changes and old readers work through native owners. A read-rich UserID AVX2 history including its final fold takes 1.69 versus native 2.98 ms. | The same UserID AVX-512 cell loses; most warm cells and every update-dominated mixed cell lose. Faster isolated repair and fewer allocations do not close the complete program gap. [Zen 5 phases](lifecycle/ZEN5.md), [program costs](lifecycle/PROGRAM-COSTS.md). |
| Ownership and changes | Borrowed extents/grants, dictionary editions, actual Orbital Parts, same-version adoption, retained readers and retirement are executable. Sustained histories preserve answers through 65 command batches and real publication ages. | Sustained checks are **semantics**, not a fold cadence or tail-latency result. Physical reclaim grain has a functional witness, not a measured optimum. Durable/replay and concurrent-writer protocol questions remain with their owners. [Material](execution/MATERIAL.md), [cycles](lifecycle/CYCLES.md). |
| Fusion | Selected consumers and in-place prefix sums demonstrate useful fusion. Multi-aggregate code at `6321ff6d` checks shared reconstruction plus specialised closed forms. Local SUM+COUNT timings take 0.58–0.74× ordinary separate calls on OS/EventTime/URLHash/WatchID. | These [fused timings](encoding/README.md#fused-aggregates) are local ARM direction only (`da1941b3`): native fused remains 1.67–2.27× faster on those columns; UserID's already-cheap COUNT gains nothing, and cached arrival can lose. MIN/MAX is checked, not timed. Predicate-mask emission, the runs prototype and wider ISA execution still need qualification. |

These cohorts are not interchangeable. Local ARM rankings, repeated worker
scans, explicitly flushed Zen 5 inputs and lifecycle phase timings answer
different questions. Flushing is input conditioning, not a measured memory-bus
throughput result. Exact wide sums must retain their arithmetic law; beating a
Polars widening/casting path while losing to native exact SUM is not a best-
program result. The [scan evidence](scan/FINDINGS.md#qualification-and-recovery)
and [read audit](ORDER-AUDIT.md) retain those comparison limits.

The run-heavy SUM losses describe the **current consumer**, not a demonstrated
lower bound for run bodies. The initial attribution to the geometry walk and
wide arithmetic did not survive the owner's local substitutions; narrower
run-value decoding is now the promising leaf change. That prototype does not
yet qualify a native improvement or attribute the remaining gap. By contrast,
predictor NULL fill's construction penalty is measured: 3.6–7.6×
slower once most blocks hold a NULL. Smaller bodies must repay their build and
maintenance bill as well as their read cost.

## Coverage of the original surface

| Family | Investigated here | Large uninvestigated portion |
| --- | --- | --- |
| Encode / unpack / transcode | Real integer bodies, point/range reconstruction, several dictionaries, checkpoint choices, nullable fill, conversion through folds/thaw. | Priors and bad-guess costs; complete writer/decoder peer comparisons; streaming drift; useful direct converters; typed floating-point/ALP and high/low splits. |
| Movement | Independent points, range reads, selected consumption, replacement/presence writes and private command batches. | General gather/scatter/append, streaming tail → seal → growth, output layouts and complete movement pipelines. |
| Compare / selection | COUNT ≥ and equality, selected reductions, heads/bounds, empty/coarse grants; predicate-mask emitter in progress. | Complete multi-column predicate → payload programs, broader Boolean predicates, maintained evidence, all-family movement savings. |
| Search / lookup | Run geometry and checkpoint searches inside particular consumers. | General Frame find/index-of/bounds/rank-to-offset programs and encoded lookup consumers. Existing Ikea primitives are ingredients, not evidence for these Frame operations. |
| Transform | Specific FOR, dictionary and prediction laws; projected maps in the grouped witness. | General arithmetic, bitwise and hash transforms; partial/throwing function legality; map lifting and reusable transformed results. |
| Reduction / scan | SUM/COUNT dominate; one grouped witness; MIN/MAX appear in the newly qualified fused implementation. | Segmented reductions, prefix/window scans, richer aggregate states and deterministic floating-point reductions. No performance claim yet for fused MIN/MAX. |
| Reorder | Reordered **inputs** establish how strongly order matters; block folding rebuilds bodies. | Actual encoded permute/partition/sort/merge/compact operations. Preparing a shuffled corpus is not a Frame reorder implementation. |

Across these families, SeriesPack is heavily exercised; Frame's TuplePack field
group work is not. Jena decimal measurements use scaled integers, not an ALP or
floating-point implementation. IMDb is a small nullable smoke test, not a
representative string study. Prefixes/lengths remain possible Frame columns;
string bodies stay with StreamPack. Row-policy/access enforcement and the broad
coarse-mask requirement have not received full-surface qualification. Joins
remain Engine's work; this map does not begin a join design.

## Have we found the metadata knee?

Not yet. We have several local tradeoffs, enough to reject “more metadata is
always better,” but not enough to select a small, broadly useful description.
Removing retained per-block decoder bindings cuts substantial memory without
fixing the point gap. H16 can buy a useful filter but costs two bytes/row;
16-byte block bounds can be sufficient elsewhere. Checkpoint density changes
point cost, while full weighted SUM does not need checkpoints at all. Dictionary
preparation can transform repeated equality and swamp the column's own bytes.

The next description experiment should therefore charge three different things:
required image interpretation, optional persistent query evidence, and reusable
execution preparation. Test whether each extra fact changes an algorithm,
avoids acquisition or amortises preparation through a complete workload. A
smaller descriptor which merely selects the same slow program is not the knee;
neither is a rich graph demonstrated only against scalar getters.
Stored evidence/geometry and prepared bindings/cache/scratch need separate
accounts: removing bindings does not establish the durable metadata knee.

A cheap first candidate is the already-computed block-kind census plus the
available operation laws: which blocks admit a closed form, search, narrow
reduction or full reconstruction? Test whether that information ranks the
existing qualified programs before adding richer descriptions. It cannot by
itself explain dispersed acquisition, field relationships or lifecycle debt;
those counterexamples tell us which further information earns its place. This
is a proposed discrimination using existing evidence, not a validated cost model.

## Decisions Engine should drive

The high-value description is one which lets Engine choose a different program.
A list of codec names is insufficient; a compulsory graph of every machine-level
stage would expose choices that have not earned their cost.

| Decision | Why it matters now | Division of responsibility to investigate |
| --- | --- | --- |
| Logical laws and field demand | Exact integer/float arithmetic, NULLs, order, equality and demanded fields determine which rewrites are legal. Duplicate projected dictionary values already break an assumed equality inverse. | Engine supplies semantics and demand; Ikea supplies operations, explicit premises and total fallbacks. |
| Row order and field relationships | Row order is the largest measured byte lever. Shared codes help one joint query and hurt independent demand; marginal statistics alone miss that. | Engine chooses clustering and independent versus coupled fields. Ikea supplies legal local representations and their access/update consequences. |
| Representation **and** execution program | Minimum bytes, fastest scan, fastest point and cheapest write need different choices. The large native point advantage is a reason to test native/point-oriented forms, not just tune getters. | Engine chooses for the workload/reuse horizon; Ikea curates useful algorithms and chooses equivalent local width/ISA implementations. |
| Retained structure and reuse | Code-domain partial results, selection masks and prepared dictionary material can avoid repeated logical translation. Preparation can also cost more than execution. | Engine decides reuse and memory budgets; Ikea describes result meaning, preparation dependencies and lifetime invalidation. |
| Maintenance and workload phases | Accumulated correction debt, fold cost, plain writable tails and retained-reader age change the best form. | Engine chooses policy from complete costs; Ikea supplies repair/fold/thaw alternatives. Orbital owns coherence/backing and Loom owns acquisition/scheduling choices. |
| Logical publication/coherence unit | Frame material inside a Segment representation and independently versioned Frame Objects impose different C/X/dictionary/summary publication and version-discovery costs. | Engine must drive this with Orbital and Ikea. The [review](REVIEW.md) keeps both alternatives open; the current physical owner witness does not decide between them. |
| Grain and placement | Logical blocks, execution batches, evidence grains, re-encode units and reclaimable backing need not coincide. One large physical owner can defeat independent retirement. | Engine expresses demand/lifecycle needs; Ikea exposes accessed and damaged regions; choose physical ownership with Orbital and residency/staging with Loom. |

The [fidelity study](execution/FIDELITY.md) gives a useful separation: logical
schema, actual image and resolved parameters, proved facts, prepared execution,
and empirical costs. Binding unchanged bytes, changing future encoding policy
and migrating existing bytes are three different decisions. Do not hide them
under one “change representation” operation.

We have enough evidence to reject an opaque **getter-only** boundary. We do not
have evidence to reject opaque complete operations or to justify a universal
planning graph. The next witnesses must show where exposing a particular
relationship changes Engine's choice. A block-kind switch invalidated by a new
codec is a real warning against parent code duplicating the child's internals.

Multi-field changes also need to distinguish the current logical category from
the old routing/conditioning basis until unchanged values have been converted.
The existing category-only edit stays within its domain. It does not qualify
unseen categories or streaming insertion into a shared dictionary. Stable IDs
with an order view, an immutable base with extensions and sorted recoding remain
competing programs, with cross-part editions and retained readers in the cost.

## Which things should stay together?

Three meanings of “together” must stay distinct: required semantic/coherence
dependencies, profitable execution fusion, and shared physical placement.
Only the first is unconditional once a representation is chosen.

| Combination | Present conclusion | Useful boundary to retain |
| --- | --- | --- |
| Coarse/fine admission → address acquisition → consumer | Admission must precede excluded accesses. Selection expresses demanded output, not read authority: predictors and reference codes may need earlier data, still covered by a caller's valid grant. | Carry original coordinates and exact dependency grants through operations; do not expand every coarse mask into rows. A semantic dependency need not imply serial fetching when addresses are already known. |
| Unpack or prefix reconstruction → immediate reduction/point result | Keep transient reconstruction inside the consumer when it removes a widened array or repeated pass. Selected algebra and native prefix gains support this. | Closed forms should bypass reconstruction entirely. Do not force weighted delta SUM through a shared prefix decoder. |
| Patches/overrides → logical contribution | Exceptional and corrected values must participate under the logical operation's law; summing physical codes or stale base values is insufficient. | Interpretation, change records and summaries need coherent editions, not necessarily one allocation or one generic patch pass. |
| One body pass → several requested aggregates | Shared narrow lanes and decoded codes improve local SUM+COUNT programs where separate operations scan the same planes; the implementation preserves run/constant/monotone shortcuts. | Qualify native timings per kind. UserID's closed-form COUNT adds almost no separate work; cached arrival's sequential SUM/truth bits can beat the fused gather. Large aggregate state can add cost. |
| Dictionary preparation → repeated execution | Preparing predicate truth/weights once per identity can be valuable, but building it inside every block operation repeats work. | Keep preparation independently reusable and charged. Function totality, parameters and edition identity determine safe reuse. |
| Code histogram/reduction → projected map → logical output | Retaining local codes through cardinality reduction helps OS grouping. Large dictionaries or sparse demand also need touched-code/direct logical-hash alternatives. | This does not establish compulsory histogram/map/hash fusion. The native control itself stages a histogram; a same-packed-image fused-versus-exposed comparison is still needed. |
| Head/bounds evidence → exact refinement | Evidence should prune work before suffix/body acquisition. Its meaning must be independently interpretable. | Choose evidence separately from body encoding. Logical heads beside a predictor and heads of prediction residuals are different information. |
| Predicate(A) → selection → payload(B) | Very important prospective seam, still unmeasured end to end here. Fusion may avoid mask materialisation; keeping the mask may permit reuse and earlier coarse skipping. | Compare both. Field locality, mask reuse and A/B validity determine the useful cut; do not assume one-row-at-a-time fusion is best. |
| Encoded image → dictionary/predictor/validity/corrections | These must describe one coherent logical snapshot. Logical version alone does not identify the physical interpretation after adoption. | Separate required interpretation, optional persistent evidence and derived execution preparation. Their publication/invalidation rules differ. |

This suggests **larger fused execution units under a composable semantic
description**, with reusable boundaries for preparation and structured results.
It is a research direction grounded in the above witnesses, not an API proposal.
Yield/prefetch boundaries may instead follow descriptor → dependent body →
dictionary/correction acquisition; a byte-codec stage is not automatically the
right Loom scheduling unit. That staging has not yet been measured for Frame.
Likewise, issued-byte damage proves coverage, not lock or isolation authority;
the isolated-copy lifecycle and journal checks establish their bounded parts.

Construction seams are less established than read seams. The current analysis
can try several candidates to minimise body bytes; the NULL-fill cost exposes
why that is not a finished ingest strategy. Residual generation → outlier
classification → packing is a plausible fused emission unit, and routing →
NULL elimination could share a coordinate/admission pass. Neither has a complete
measured comparison here. Keep the choice of recipe, its resolved parameters
and its emission distinguishable; do not turn a speculative stage list into
mandatory intermediate arrays or pretend its fusion benefits are established.

The earlier [residual-bounds investigation](../../notebook/pfor-bounds.md)
sharpens this seam. Estimation can supply one cheap proposal, several
width/exception alternatives or refinement of a prior; its enclosing program
must define the useful objective. A fixed exception budget and minimum bit
width do not price physical layout, metadata, reserve or subsequent operations.
Ragged geometry can even make a narrower width occupy more bytes. A guess may
pass its exception budget while wasting many bits, so recognition must detect
poor compression as well as invalid guesses. Base and width priors deserve
separate treatment across distribution changes. Low exceptions can absorb later
below-base writes without rebasing, making reserve a lifecycle choice; inlier
bounds still do not substitute for all-row query summaries. These are retained
qualitative lessons: the discontinued spike's artifacts were deleted and it did
not establish a performance win. They do not select an estimator or patch format.

## Map for the remaining investigation

The following are connected research fronts, not sequential gates or a demand
for one giant Cartesian-product benchmark. [QUESTIONS.md](QUESTIONS.md) remains
the detailed question index. Each front needs a comparison capable of changing
a choice; repeating hot SUM rankings alone will not cover the missing ground.

| Front | Discriminating next work | Decision the result can change |
| --- | --- | --- |
| Construction and fitting | Compare cheap proposals, separately informed base/width priors and refinement against an exact quality oracle and actual physical/program costs. Charge analysis, bad-guess recognition, fallback, packing and later writes/reads; include real distribution changes. | Does the caller need one fitted choice, a small set of useful alternatives or incremental refinement? Which analysis/emission boundaries earn fusion, and when is a small byte penalty worth much cheaper construction? |
| Useful execution programs | Finish the bounded fused-aggregate measurement and runs prototype; test existing wider execution grain on unchanged images. Retain compact native controls and closed-form arms. | Which costs are replaceable leaf programs, and which require a different representation? Avoid another kernel library or a wholesale catalogue default. |
| Selection and data acquisition | Complete predicate(A) → consumed SUM/COUNT(B), contrasting clustered/scattered selections and independent/correlated fields. Compare retained-mask and fused execution; then stage dependent acquisition, enlarge real inputs and test shared-bus load. | Is the useful seam a block mask, fine mask, retained evidence or fused query? When do compressed bytes become a real cold/bandwidth advantage? |
| Multi-field choices | Put independent columns beside shared codes, conditioned bases, routing and actual TuplePack field groups. Include isolated-field demand, duplicate projections, category-only changes and unseen category/dictionary values; retain old routing bases and reader ages. | Which dependencies should Engine know and choose? When does coupled encoding save work versus merely shrink a joint query while taxing everything else? |
| Change and ownership | Price sustained fold/thaw policies on the qualified histories; add streaming append/tail sealing, evidence maintenance and physical copy/reclaim grain. Preserve reader ages and phase changes. | When should a region stay native, accept corrections or reseal? Which grains and shared dependencies survive sustained writes? No default from eight batches. |
| Breadth of operations and types | Add distinct witnesses from the uncovered families: transformed results, search, segmented/prefix/window reductions and encoded reorder; build typed float/decimal and string-metadata cases. Reuse existing Ikea capabilities and Calico negatives. | Which representation laws generalise beyond SUM/COUNT? Which curated units serve many operations, and which are specialised endpoints? |
| Planning information | Start with census × lawful operations. Hold images and available operations fixed, vary the relation visible to a chooser, and compare with a small manually enumerated best-program control. Charge stored evidence separately from preparation and maintenance. | Does richer information actually select a better complete program? The same-algorithm rich/opaque comparison and manually authored shared-code wins do not establish that. Find the metadata knee through useful choices. |

The immediate priority is to close the already-running bounded owner cuts and
the selective A→B witness, then broaden the evidence rather than polishing the
same narrow surface indefinitely. Encode/ingest cost and floating-point laws
deserve dedicated attention; they cannot be inferred from integer scan wins.
Physical lifecycle and multi-field work should challenge the encoding choices
while they are still cheap to change. Native is a representation candidate,
not a fallback outside Frame's ambitions.

## Working checkpoint

Ikea LEAD owns this synthesis, scans/points/filter consumers and lifecycle;
Claude owns encoding/algebra and the active fused/predicate/runs cuts; Engine
owns execution witnesses and the interpretation of composition seams. The
[working research map](RESEARCH.md) tracks that division. Ikea HELPER owns
Intervals and current Ikea interface cohesion; a separately frozen Frame caller
qualification is pending. Old source receipts do not qualify the migrating APIs.

This stocktake adds no timings and pools no cohorts. It redirects investigation
without retiring negative results, narrowing the original brief or promoting
the research into a maintained Frame module.
