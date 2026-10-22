# Frame research questions

Question index for the [Ikea Frame spike](README.md). **Experiment** questions
are settled by measurement on real workloads. **Design** questions concern
semantics, policy or ownership, informed by experiments and the owning module;
the proposals here do not establish contracts. Resolved questions move into the
owning note and leave this list.

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
| How well do priors with bad-guess detection choose the split point and frame-of-reference bounds, and what does recognising a bad guess cost? | Experiment | Analysis runs on every re-encode; a valid but wasteful guess is the known failure mode. | [Residual bounds note](../../notebook/pfor-bounds.md), [Encoding §4, §7](ENCODING.md#7-frame-of-reference-and-exceptions) |
| On which integer columns does a high/low split pay, and at what split points? | Experiment | ALP_RD's case is floats; the integer case (timestamps with jitter, IDs with random suffixes) is unmeasured. | [Encoding §4](ENCODING.md#4-split) |
| Is whole-block opt-out from ALP too coarse? One NaN or over-precise value sends 255 others to the float map, where ALP itself would patch it. | Experiment | Decides whether a small logical-level exception list is worth a second mechanism. | [Encoding §3](ENCODING.md#3-working-domain) |
| Does frame of reference beat plain packing at real column widths under SeriesPack's tile geometry, once exceptions are counted? | Experiment | Narrower is not always smaller with ragged tiles. | [Residual bounds note](../../notebook/pfor-bounds.md#what-sixdb-composition-changes) |
| Can exception application fuse with SeriesPack reads to avoid Calico's patch-decode penalty? | Experiment | Calico's patched decode was 1.9–4.7× slower until fused. | [Survey §2](SURVEY.md#2-calico) |
| Relative Ends checkpoints or Sizes-style prefix sums inside predicted blocks? | Experiment | Point-read cost versus bytes per value. | [Encoding §5](ENCODING.md#5-map-or-predict) |
| How should predicted-block checkpoints be stored, and how often? On Jena, 15 native u32/u64 checkpoints per 256-row block cost about 2 bits/value and flip delta from 85% to 35% of blocks (147 vs 128 bits/row). Bit-packing at K + 8 bits, or spacing every 64, trades point-read cost for bytes. | Design / experiment | Decides whether the decided 16-value checkpoints let prediction pay. | [Encoding inventory, Jena](encoding/README.md#jena-prediction-in-real-bodies) |
| How should predicted-block checkpoints handle exceptions: widen to the largest cumulative exception, or exclude exception contributions and re-add them from the exception list? | Experiment | One full-width exception otherwise breaks the `K + 8` bound for every later checkpoint. | [Encoding §5](ENCODING.md#5-map-or-predict), [review cautions](REVIEW.md#arithmetic-and-ownership-cautions-for-the-prototypes) |
| Do linear and category predictors pay on SixDB's columns? | Experiment | Calico's predictive transform paid only on one telemetry corpus. | [Survey §2](SURVEY.md#2-calico) |
| Should Boundlist be renamed for its third consumer? Its endpoint searches already accept wide ordered sources, so prototyping does not depend on it. | Design | Naming only; the review found no missing search law. | [Encoding §5](ENCODING.md#5-map-or-predict) |

## Coarse masks (Ashton, 2026-10-22)

Most operations should accept a coarse filter mask (for example one bit per
block or group) and skip descriptor, payload and downstream work where lawful.
No fixed grain or API is chosen. See [Encoding, coarse masks](ENCODING.md#coarse-masks).

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| How much does a coarse mask save across all, none, sparse and clustered block masks, for scans, sums, gathers and decodes, against row-mask-only execution? | Experiment | The requirement is to skip data movement, not just computation. | Ashton via Codex |
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
| For write-heavy sorted columns, is rewriting one 16-value sub-block cheaper than sending every write to a predicted block into overrides? | Experiment | Every write to a predicted block currently becomes an override. | [Encoding, Writes](ENCODING.md#writes) |
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
| Which representation pairs earn a direct converter rather than going through a canonical form? | Experiment | Orbital's `adopt` requires exact logical equivalence; n² converters do not scale. | [Survey §2](SURVEY.md#2-calico) |
| Is an encoded/plain bitset over blocks all Frame needs to describe a part's block mix, with any richer divergence recorded by Engine? | Design | Engine owns the schema; Ikea supplies materials. | [Encoding §6](ENCODING.md#6-block-kind) |
| How do grouping, merging and joins work across parts whose dictionary codes differ? | Design | Part-wide codes are only comparable within one part. | [Survey §5](SURVEY.md#5-lessons-to-carry-forward) |

## Beyond encoding (set aside for now)

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| Is row-level access control a predicate composed like any filter (a policy mask ANDed into every read), or a separate enforcement surface inside Frame? | Design | A large contractual surface where kernels meet user requirements. | [README](README.md#starting-context-from-discussion-with-ashton) |
| What is Frame's public query interface for SeriesPack and TuplePack? | Design | Frame owns a significant share of the operation families. | [README](README.md#starting-context-from-discussion-with-ashton) |
