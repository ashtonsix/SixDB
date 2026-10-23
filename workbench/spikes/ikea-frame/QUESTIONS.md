# Frame research questions

Question index for the [Ikea Frame spike](README.md). **Experiment** questions
are settled by measurement on real workloads. **Design** questions concern
semantics, policy or ownership, informed by experiments and the owning module;
the proposals here do not establish contracts. Resolved questions move into the
owning note and leave this list. The [stocktake inventory](INVENTORY.md) holds
the current strengths, losses and uninvestigated areas; this file keeps the
detailed open questions behind them.

## Architectural research

The [review with Engine](REVIEW.md) develops the broader questions before the
encoding details below. Its [competing experiments](REVIEW.md#research-programme-to-discriminate-the-architectures)
connect schema fidelity, retained structured results, selected residual algebra,
metadata/evidence, cross-column dependencies, mutation phase changes and physical
ownership. They are alternatives to investigate, not an agreed architecture or
a sequence of promotion gates. Encoding remains the initial emphasis.

Reading notes on the review (2026-10-22), where it needs qualifying:

- **Bit-plane masked SUM applies to Local residual planes only.** SeriesPack's
  Local tail bits are bit planes, but body bytes are byte-wise and Striped
  payloads interleave rows by stripe lane. A mixed-width tile needs a byte-plane
  sum for its body plus the popcount sum for its tail; Striped needs its own
  derivation. The experiment should sweep layout as well as K.
- **Weighted predicted SUM and checkpoints.** The weighted form uses every
  residual in the block, so checkpoints are redundant for it; it competes with
  checkpoint-restarted prefix decode rather than depending on it. Wrapping
  residuals need the review's wrap corrections for an exact wide sum.
- **The writable plain state and the streaming tail are the same state.** The
  review's thaw/reseal lifecycle and Ashton's streaming collection both hold
  plain blocks awaiting re-encode, so one mechanism can serve both. Forced
  compaction when slack runs out may produce plain blocks, not only encoded
  ones; that keeps Ashton's slack rule while avoiding re-encode thrash.
- **Taxonomy versus algorithms.** Agreed: ENCODING's "what each stage preserves"
  table is a coarse summary, not a planning interface.

## Encoding

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| How should a part-wide dictionary respond to streaming and shifting data? Sorted dictionaries renumber codes when values are added at re-encode; drifting distributions age the dictionary. Candidates: re-encode the part's codes, leave gaps in the code space, keep new values unmapped until a whole-part rebuild. | Experiment | Decides whether order-preserving codes survive streaming without whole-part rewrites. | [Encoding §5, Streaming](ENCODING.md#streaming) |
| Which row order should a table take? On ClickBench, real bodies cost 179 bits/row in original order, 294 shuffled and 238 re-clustered by (RegionID, OS, EventTime); re-clustering makes some columns nearly free and others 3–17× larger. | Design / experiment | Order moves bytes more than any per-block codec choice; Engine owns it, and lifecycle inserts erode it. | [Encoding, row order](encoding/README.md#row-order-in-real-bodies) |
| Sorted, frequency or arrival part dictionaries, per column? Frequency is up to 0.4 bits/value smaller on low-cardinality columns but slows COUNT up to 1.45× and loses code order. Arrival brings URLHash to 43.9 bits/value (sorted 50.9) but costs 1.3–3× sorted's scans; with a warm cache its equality COUNT beats sorted. Which workloads (equality-heavy hashes, ranges, full scans) select each, and at what cache budget? | Experiment | A curated option for Engine unless one wins clearly. | [Arrival dictionaries](encoding/README.md#arrival-order-dictionaries), [caches](encoding/README.md#dictionary-caches-across-calls) |
| Does an entropy-coded cold representation repay its decode, point-read and conversion costs where row order doesn't help? In the shuffled model it reaches Parquet+ZSTD parity. | Experiment | Bytes alone don't justify it; operations must. | [Encoding, second pass](encoding/README.md#second-pass-frequency-codes-entropy-and-row-order) |
| What closes the remaining 2.5 bits/value between arrival dictionaries (URLHash 43.9) and ZSTD-3 (41.4): multi-row matches, or entropy coding over the literal plane, which carries 36.6 of the 43.9 bits? | Experiment | Recent-value references are now real as arrival dictionaries; the residue is literals, not repeats. | [Arrival dictionaries](encoding/README.md#arrival-order-dictionaries) |
| Is the run-heavy full-SUM loss (Zen 5 flushed OS 483 vs native 81 µs) a leaf cost or a representation limit? Local A/B rules out the u128 multiply-add and the Intervals walk as the cost (64-bit accumulation and direct u8-end walks read within noise). It is mostly the lane: [run-value programs](encoding/README.md#run-value-programs) (05463eae, local) decode into the narrowest catalogue lane and take run-heavy SUM to 0.65–0.78× and COUNT to 0.19–0.37× (COUNT then 0.9–1.2× plain). Byte-lane run widths read in place reach 0.47–0.60× on SUM (1.2–1.5× plain), at +0–24% bytes and with weaker width bounds (an above-max COUNT on ResolutionWidth goes 7–9× slower). Narrow stays opt-in: it lost on EventTime's masked rare equality (1.05–1.3×, not yet attributed) and on K = 1 SUM (1.02×). Worker timings and the remaining SUM gap to plain are open. | Experiment | If the leaf can't fall below memory delivery, run bodies win cold scans only through algebra (COUNT), not bytes. | [Controlled scans](scan/FINDINGS.md), [inventory](INVENTORY.md) |
| Do fused multi-aggregate passes pay, per kind and on native ISAs? Locally (`da1941b3`, direction only), SUM + COUNT runs at 0.58–0.74× the separate calls on packed and run columns, with no gain where a closed form decides an aggregate (UserID COUNT, delta) and for cached arrival dictionaries; native fused stays 1.7–2.3× faster. Native qualification is pending. Closed forms differ per aggregate on runs and monotone delta, so sharing there is bounded by the run-geometry walk; packed planes share the decode. | Experiment | Decides whether Engine requests aggregate sets or separate operations. | [Inventory, fusion](INVENTORY.md#which-things-should-stay-together) |
| How well do priors with bad-guess detection choose the split point and frame-of-reference bounds, and what does recognising a bad guess cost? | Experiment | Analysis runs on every re-encode; a valid but wasteful guess is the known failure mode. | [Residual bounds note](../../notebook/pfor-bounds.md), [Encoding §4, §7](ENCODING.md#7-frame-of-reference-and-exceptions) |
| On which integer columns does a high/low split pay, and at what split points? | Experiment | ALP_RD's case is floats; the integer case (timestamps with jitter, IDs with random suffixes) is unmeasured. | [Encoding §4](ENCODING.md#4-split) |
| Is whole-block opt-out from ALP too coarse? One NaN or over-precise value sends 255 others to the float map, where ALP itself would patch it. | Experiment | Decides whether a small logical-level exception list is worth a second mechanism. | [Encoding §3](ENCODING.md#3-working-domain) |
| Can exception application fuse with SeriesPack reads to avoid Calico's patch-decode penalty? | Experiment | Calico's patched decode was 1.9–4.7× slower until fused. | [Survey §2](SURVEY.md#2-calico) |
| Relative Ends checkpoints or Sizes-style prefix sums inside predicted blocks? | Experiment | Point-read cost versus bytes per value. | [Encoding §5](ENCODING.md#5-map-or-predict) |
| What point-read cost does each curated checkpoint option impose (native every 16, bit-packed every 16, every 64, none)? On Jena they cost 147 / 139 / 133 / 128 bits/row. In-place prefix sums now read points in 0.48–0.73× the earlier time (locally about 13–17 ns with native or packed checkpoints, 27–32 ns without), and the non-decreasing delta COUNT also needs checkpoints. Ashton: Engine decides among curated options unless one clearly wins. | Experiment | Engine needs bytes, point cost and COUNT search together to choose per column. | [Jena](encoding/README.md#jena-prediction-in-real-bodies), [delta points](encoding/README.md#delta-point-reads-through-the-catalogue-sum) |
| Should point-heavy access route to a native or sidecar representation instead of Frame getters? Native arrays remain substantially faster for points, even after the prefix and run-source improvements. | Design / experiment | A choice for Engine's workload model, not further getter tuning by default. | [Inventory, points](INVENTORY.md#evidence-inventory) |
| How should predicted-block checkpoints handle exceptions: widen to the largest cumulative exception, or exclude exception contributions and re-add them from the exception list? | Experiment | One full-width exception otherwise breaks the `K + 8` bound for every later checkpoint. | [Encoding §5](ENCODING.md#5-map-or-predict), [review cautions](REVIEW.md#arithmetic-and-ownership-cautions-for-the-prototypes) |
| Do linear and category predictors pay on SixDB's columns? | Experiment | Calico's predictive transform paid only on one telemetry corpus. | [Survey §2](SURVEY.md#2-calico) |

## Separated filter heads (Ashton, 2026-10-22)

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| Which transform cut gives filter heads both useful entropy and an independent predicate meaning: block-late FOR, a part-level FOR cut, or the early working value? | Experiment | Block-late heads carry the most entropy but need per-block threshold translation; part-level heads scan uniformly. | [Encoding, filter heads](ENCODING.md#separated-filter-heads-ashton-2026-10-22), [filter consumer](filter/README.md) |
| What do heads mean for predicted, dictionary-coded and patched blocks (value heads from an earlier cut, order-preserving code heads, frequency-class heads, exception merging)? | Design / experiment | Residual heads don't describe values; exception slots mislead. | [Encoding, filter heads](ENCODING.md#separated-filter-heads-ashton-2026-10-22) |
| Evidence plane (value heads beside an unchanged predicted body) versus true split (part-level heads, prediction only on the low part): beyond bytes, price circular centring of modular low residuals (a −1 step is 2^s − 1 unsigned), weighted SUM, which doesn't survive per-row modular reconstruction without wrap corrections or a prefix scan, and coherent publication of both planes on writes. | Experiment | The two arms likely tie on bytes; these obligations decide them. | Codex and Claude, 2026-10-22 |
| How stable is a head's meaning across re-encodes, when block K and base change? | Experiment | Lifecycle re-encodes would invalidate any cached head predicates or statistics. | Ikea LEAD's lifecycle work |

## Coarse masks (Ashton, 2026-10-22)

Most operations should accept a coarse filter mask (for example one bit per
block or group) and skip descriptor, payload and downstream work where lawful.
No fixed grain or API is chosen. See [Encoding, coarse masks](ENCODING.md#coarse-masks).

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| How much does a coarse mask save across all, none, sparse and clustered block masks, for scans, sums, gathers and decodes, against row-mask-only execution? Coarse and row-selected laws now exist and are qualified for SUM and COUNT (all seven kinds, empty selections touch no body). Gathers, decodes, predicates, transforms and the other families remain. | Experiment | The requirement is to skip data movement, not just computation. | Ashton via Codex; [selected aggregates](encoding/README.md#row-selected-aggregates-and-null-fill) |
| Where should the predicate(A) → selection → payload(B) seam sit: an emitted block or row mask, retained evidence, or one fused operation? Compare clustered and scattered selections, independent and correlated fields, and mask reuse. The owner mask emitter landed at 3f31a329 ([predicate masks](encoding/README.md#predicate-masks)): exact for every kind and dictionary order, end-to-end into masked SUM/COUNT on B; run-heavy masks still trail a plain array 2–6× locally. | Experiment | Selective movement is the cold-scan thesis; same selected values scattered already lose flushed (131.6 vs native 51.1 µs). | [Inventory](INVENTORY.md), [scan findings](scan/FINDINGS.md) |
| What dependency halo does each stage impose on skipping (prediction back to its checkpoint, rank prefixes for compacted or routed rows, dictionary entries, override slices), and what does carrying it cost? | Design / experiment | A skipped block may still be needed by an admitted one. | [Encoding, coarse masks](ENCODING.md#coarse-masks) |
| How do coarse and row masks compose, and can a coarse mask be reused across the phases of one operation or across queries? | Experiment | Recomputing or widening masks per phase gives away the saving. | Ashton via Codex |
| Which grain is the knee (256 positions, a 4,096-position group, or several), given metadata, statistics and Orbital Part grain? | Experiment | Finer masks skip more but cost more to produce and consult. | [Writes §2](WRITES.md#what-this-means-for-frames-encoding) |

## Writes, overrides and compaction

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| What does an override cost on reads as its density rises, and how much slack should a group reserve? | Experiment | Sets the forced-compaction rate and the read tax of a hot group. | [Encoding, Overrides](ENCODING.md#overrides-mutable-outliers) |
| What does re-encoding one group cost, and what should trigger it besides exhausted slack or a full streaming tail? | Experiment | Streaming re-encode and forced compaction share this operation. | [Encoding, Streaming](ENCODING.md#streaming) |
| Should a routed column keep one override store per group, or one per route? | Design | Overrides hold logical values, but routes encode separately. | [Encoding §2](ENCODING.md#2-routing) |
| How are category writes handled under routing: value overrides, immediate route repair, or a conditioned base that keeps original coordinates? | Experiment | A category change makes unchanged values decode against the wrong route. | [Encoding §2](ENCODING.md#2-routing), [review §5](REVIEW.md#5-put-cross-column-alternatives-beside-category-routing) |
| Fitting packed and delta repairs now exist (an interior delta edit changes two residuals and at most one checkpoint). For writes that don't fit, is rewriting a 16-value sub-block cheaper than an override, and how do delta-with-exception gaps, which can touch several checkpoints, fit in? | Experiment | Not every predicted write becomes an override any more; the non-fitting remainder sets the override rate. | [Repair](lifecycle/REPAIR.md), [Encoding, Writes](ENCODING.md#writes) |
| Where do NULLs belong physically? Predictor fill costs bodies nothing but makes encode 3.6–7.6× slower when most blocks hold a NULL. Zero fill sometimes reads faster after sparse updates. Storing only valid rows pays only in packed- or delta-heavy, NULL-heavy blocks (model). A sparse validity form is unbuilt. | Experiment | Minimum body bytes is not a write policy; fill, validity form and compaction are separate choices. | [NULL fill](encoding/README.md#row-selected-aggregates-and-null-fill), [program costs](lifecycle/PROGRAM-COSTS.md) |
| Do cold parts keep the group layer, or only part and block? | Design | Groups exist for mutation; cold parts may not need them. | [README](README.md#starting-context-from-discussion-with-ashton) |
| What Part grain should a group's planes have, so an epoch republishes only touched groups (as `engine::directory` shares unchanged leaves) without per-Part metadata dominating? | Experiment | Decides publication and COW proportionality. | [Writes §2](WRITES.md#what-this-means-for-frames-encoding) |
| What physical envelope does each write outcome (in place, override, compaction) dirty, and how do logical row locks project onto it? Concurrent writers in one group share its override store. | Design | Ikea must honour locks and publish byte damage before native stores. | [Writes §3](WRITES.md#what-this-means-for-frames-encoding) |
| For group re-encodes and overrides, is replaying admitted input cheaper than capturing changed material, across hot, warm and cold readers? | Experiment | Orbital's views work recovered correctly both ways; costs differ by unit. | [Writes §5](WRITES.md#what-this-means-for-frames-encoding), [publication](../orbital-views/publication/README.md) |
| When does taking a hot group into a Draft beat copying it, given Frame's write locality and typical reader overlap? | Experiment | Take versus copy is Orbital/Loom's choice, but Frame's layout sets its cost. | [Writes, sources](WRITES.md#what-is-established-and-where) |

## Statistics and aggregates

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| Do block min/max live on the parent segment beside block sums, or in the part beside the descriptors? | Design | Block sums are already the parent's; pruning reads whichever holds extrema. | [Encoding, statistics](ENCODING.md#block-descriptor-and-statistics) |
| Which logical contribution law feeds parent sums from each stage? `sum_change` is a packed-domain modulo-u64 delta: right for plain and frame-of-reference integers, wrong for dictionary codes, splits, floats and signed values. | Design | Parent summaries must stay exact through every encoding. | [Encoding, Writes](ENCODING.md#writes) |
| Should block statistics carry precision levels (exact versus conservative) instead of a dirty bit? A shrinking write leaves the old bound safe for pruning. | Experiment | Keeps pruning useful under churn without forcing re-encode. | [Encoding, statistics](ENCODING.md#block-descriptor-and-statistics), [review §4](REVIEW.md#4-separate-compression-addressing-and-query-evidence) |
| How should floating-point arithmetic be made deterministic? Ashton's expectation: a defined operation order at group level (perhaps a blocked tree for ILP) with native arithmetic, then Kulisch superaccumulation between groups or parts so replicas can reorder by local state (for example what each has mapped). Also: a correctly rounded library such as CORE-MATH for bit-identical results across platforms. | Experiment | Equivalent representations and replicas must give identical answers; this sets aggregate kernels' shape and cost. | Ashton, 2026-10-22; [kmath superaccumulators](../../../../calico/kmath/SPEC.md) |

## Representations and Engine's view

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| What does Engine get from a capability-matrix cell: a yes/no flag, a cost model, a measured cost per platform, or a kernel handle? | Design | Decides the shape of Frame's planning surface. | [Survey §1](SURVEY.md#1-capability-matrix) |
| Does a block-kind census crossed with per-kind operation laws (closed form, partial or decode) rank the qualified scan cells correctly? It's cheap and already computed at encode, so it's the baseline any richer planning description must beat. Test it retrospectively on the existing panels before adding metadata. | Experiment | Finds the metadata knee from useful decisions instead of a registry. | [Inventory](INVENTORY.md) |
| Which operation families beyond SUM/COUNT have representation laws worth exposing? Grouping with codes as dense ids, search and bounds, transforms, segmented, prefix and window reductions, encoded reorder (permute, partition, sort, merge), and MIN/MAX from block structure. Joins stay with Engine. | Experiment | The original bar is per family; SUM/COUNT evidence doesn't cover them. | [Inventory, coverage](INVENTORY.md#coverage-of-the-original-surface) |
| What do TuplePack field groups add, and how do they compose with SeriesPack columns in one Frame part? | Experiment | Frame's TuplePack side is essentially unexamined. | [README](README.md) |
| How fast are encode, conversion and streaming ingest (simple tail, then group recompaction) against strong writers such as DuckDB and Parquet, including priors and bad-guess costs? | Experiment | Never timed against a peer; every re-encode pays it. | [Inventory](INVENTORY.md) |
| Do the encoded leaves use the target ISA's grain? The catalogue's native_ops stay at the 16-row AVX2 grain under AVX-512, while native controls use ZMM loops. How do results change across Zen 5 AVX2 and AVX-512, Graviton, and multi-core shared-bus load? | Experiment | Single-core, single-ISA rankings may misattribute codegen to representation. | [Scan findings](scan/FINDINGS.md) |
| How do typed floats and decimals encode and compute: ALP-style decimals, high/low splits, and exact or deterministic SUM and predicates? Jena's measurements use scaled integers, not floats. | Experiment | A large share of real data has no evidence yet. | [Encoding §3](ENCODING.md#3-working-domain) |
| Which representation pairs earn a direct converter rather than going through a canonical form? | Experiment | Orbital's `adopt` requires exact logical equivalence; n² converters do not scale. | [Survey §2](SURVEY.md#2-calico) |
| Is an encoded/plain bitset over blocks all Frame needs to describe a part's block mix, with any richer divergence recorded by Engine? | Design | Engine owns the schema; Ikea supplies materials. | [Encoding §6](ENCODING.md#6-block-kind) |
| How do grouping and merging work across parts whose dictionary codes differ, and across projected maps whose values repeat (the equality inverse is unsound there)? | Design | Part-wide codes are only comparable within one part; forward maps need not be injective. | [Survey §5](SURVEY.md#5-lessons-to-carry-forward), [cross-column](execution/NATIVE-DOMAINS.md) |

## Beyond encoding (set aside for now)

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| Is row-level access control a predicate composed like any filter (a policy mask ANDed into every read), or a separate enforcement surface inside Frame? | Design | A large contractual surface where kernels meet user requirements. | [README](README.md#starting-context-from-discussion-with-ashton) |
| What is Frame's public query interface for SeriesPack and TuplePack? | Design | Frame owns a significant share of the operation families. | [README](README.md#starting-context-from-discussion-with-ashton) |
