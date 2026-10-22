# Frame research questions

Every open question for the [Ikea Frame spike](README.md), in one place. Design
notes link here instead of keeping their own lists. **Experiment** questions are
settled by measurement on real workloads. **Design** questions are choices that
need Ashton's direction, possibly informed by an experiment. Resolved questions
move into the owning note and leave this list.

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
| Do linear and category predictors pay on SixDB's columns? | Experiment | Calico's predictive transform paid only on one telemetry corpus. | [Survey §2](SURVEY.md#2-calico) |
| What does Boundlist need to serve 64-bit value series as well as offsets, and what is it renamed to? | Design | Boundlist appears in four roles in the encoding. | [Encoding, Ikea primitives](ENCODING.md#reuse-of-ikea-primitives) |

## Writes, overrides and compaction

| Question | Kind | Why it matters | Source |
| --- | --- | --- | --- |
| What does an override cost on reads as its density rises, and how much slack should a group reserve? | Experiment | Sets the forced-compaction rate and the read tax of a hot group. | [Encoding, Overrides](ENCODING.md#overrides-mutable-outliers) |
| What does re-encoding one group cost, and what should trigger it besides exhausted slack or a full streaming tail? | Experiment | Streaming re-encode and forced compaction share this operation. | [Encoding, Streaming](ENCODING.md#streaming) |
| Should a routed column keep one override store per group, or one per route? | Design | Overrides hold logical values, but routes encode separately. | [Encoding §2](ENCODING.md#2-routing) |
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
