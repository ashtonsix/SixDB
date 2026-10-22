# Frame data encoding, rev 1

A proposal for how Frame encodes fixed-width scalar columns, built on
SeriesPack, TuplePack and Boundlist rather than a FastLanes-style layout. It
refines the rev 2 pipeline from the [README](README.md) using the
[prior-art survey](SURVEY.md). Nothing is implemented or measured. Where the
text says **decided** it records Ashton's direction; everything else is a
proposal to challenge.

The [architectural review](REVIEW.md) keeps wider alternatives and arithmetic/
ownership cautions visible while this proposal is explored. In particular, a
one-column encoding experiment does not settle Frame's cross-column, operation
or physical-owner boundary.

## Scope

In scope: one column of one part (2^16 physical key positions) holding unsigned
or signed integers, decimals, dates/timestamps, floats or codes, with nulls.
String prefixes and lengths enter as integer columns.

Out of scope (**decided**): string bodies and FSST (StreamPack's territory),
sets, bespoke structures and string collections, which are Frame's peers. Also
deferred here: the query interface, access control and the cost model Engine
plans with.

## Principles

1. **Representations, not formats** (**decided**, Orbital's data model). Every
   encoding of a part is a Representation of one Object. Representations of the
   same identity are logically identical and convertible; writes apply
   deterministically. Bytes may differ across replicas, so in-band priors are
   allowed. Reads must not depend on which representation served them.
2. **Engine composes, Ikea encodes** (**decided**). Engine picks the part-level
   expression and the block alternatives it allows. Ikea's encoder chooses
   block parameters within that envelope, deterministically from the input,
   the expression and supplied priors. Presets are expressions worth curating.
3. **Blocks can opt out** (**decided**). Parts and groups mix block kinds: a
   block where a stage is ineffective or invalid skips that stage, or skips
   compression entirely. Encode-time refusals are resolved by opting out, not
   by spending override slack. Overrides are for later writes.
4. **Reuse Ikea's 256-position geometry.** A 256-position block aligns with
   every SeriesPack tile (8 rows Local, 32–256 rows Striped), Boundlist's compact
   view (≤256 intervals, 16-rank checkpoint groups), StreamPack (≤256 ranks)
   and Bec256 (256-position bitsets). A 4096-position group is 16 blocks: one
   Boundlist checkpoint group.

## Geometry

| Level | Positions | Role |
| --- | --- | --- |
| Part | 65,536 | The Object. Fixes logical type, presence policy, routing, the part-wide dictionary (**decided**) and the expression skeleton. One Orbital allocation when hot. |
| Group | 4,096 (16 blocks) | Unit of mutation slack, override storage, compaction and (when hot) page ownership and COW/remap. |
| Block | 256 | Unit of encoding choice: which stages apply, kind, parameters, widths, exceptions, statistics. Independently decodable. |
| Sub-block | 16 | Checkpoint grain inside predicted blocks. |

A block's payload size varies with its kind and width, so block bodies inside a
group are located by a **Boundlist Sizes geometry over 16 ranks** (one checkpoint,
the group's origin). A cold part can keep this layout with zero slack; hot and
cold parts then differ in placement and slack, not in format, and converting
between them is a repack.

## The pipeline

Encoding runs top to bottom; decoding runs bottom to top. The part fixes which
stages are available and their shared state (the dictionary, routes); each
block records which of them it uses. A block that opts out of everything is
**plain**: the working-domain values packed at full width, which every
representation can always fall back to.

| # | Stage | Part fixes | Block chooses | On encode-time refusal |
| --- | --- | --- | --- | --- |
| 1 | Presence | dense, dense + null mask, compacted | — | — |
| 2 | Routing | none, by category column | — | — |
| 3 | Working domain | logical type | floats: ALP (e,f) or float map | Opt out to float map |
| 4 | Split | allowed, prior split point | apply or not, point | Opt out |
| 5 | Map *or* predict | dictionary (sorted or by frequency) | use codes or not; predictor none / previous / linear / category | Opt out to unmapped values |
| 6 | Block kind | allowed kinds | plain, constant, runs, frame of reference | — |
| 7 | Frame of reference | — | base, width K | Exception (width outliers) |
| 8 | Pack | preset policy | SeriesPack format, optional head | — |
| — | Overrides | — | group store, per-block index | Later writes only |

### 1. Presence

- **Dense**: every position stores a value. Nulls take a filler value the
  encoder chooses freely (the previous value, or the block base), so they
  cost nothing in width or exceptions. A per-block null mask (Bec256 cold, plain
  32 B hot) records which positions are null; blocks without nulls omit it.
- **Compacted** (sparse columns): only present values are stored. Position *p*
  maps to index `rank(mask, p)`. The compacted series is cut at block
  boundaries, so block *b* owns `popcount(mask_b)` values; their offsets within a
  group are another 16-rank Boundlist Sizes geometry.

### 2. Routing

Routing by a category column is compaction keyed by category instead of by
presence (**decided** to fuse them). Route *r* stores the values at positions where
`category = r` (and present). A position's index within its route comes from
per-block per-route counts plus `popcount(filter<equal>(category, r))` over the
block prefix, which current SeriesPack filters already compute. Each route
then runs its own stages 3–8 with its own priors, which is the point: value
domains differ by category. Runs of one category become cheap through stage 6
on each route.

Routing makes the value column depend on the category column. A category
write moves a row between routes even when its value is unchanged: with
categories `[A,B]`, values `[10,20]` and routes `A:[10], B:[20]`, changing the
first category to `B` makes the old route data decode wrongly unless the value
is repaired. Category writes therefore become value overrides (or route
repair), and the routing basis the data was encoded against is distinct from
the current category until the group re-encodes. Conditioned bases, shared
codes and other cross-column alternatives that keep original coordinates are
compared in the [review](REVIEW.md#5-put-cross-column-alternatives-beside-category-routing).

### 3. Working domain

Everything below this stage is unsigned, so all of Ikea's unsigned filters and
packing apply.

- **Signed integers: offset binary, always.** Flipping the sign bit costs one
  XOR and preserves order. This is the only signed-to-unsigned conversion in the
  pipeline: prediction uses wrapping arithmetic and frame of reference removes
  the remaining offset (stage 5).
- **Floats, lossless default: float map.** The monotone bit map (as in kmath's
  `floatmap.h`) orders all IEEE patterns as unsigned integers.
- **Floats, decimal: ALP-style.** Per block, an exponent/factor pair turns each
  value into an integer whose round trip reproduces the exact bits. A block
  where any value fails the round trip (NaN, −0, infinities, excess precision)
  opts out to the float map. Order is preserved within a block's (e,f), and
  across blocks once values return to the float-map domain.
- Decimal(p,s), dates and timestamps are already integers.

### 4. Split

`u = hi·2^s + lo`. The low `s` bits go to their own SeriesPack plane, raw; the
high part continues through stages 5–8. This is ALP_RD's trick generalised to
integers (timestamps with jitter, IDs with random suffixes). It is a
SeriesPack `value_expression` with a substituted tail child: the logical node
already exists. The split point comes from priors, with a per-bit entropy check
on the block as bad-guess detection. Ikea's existing head planes are the mirror
image (top bits separated for filtering) and can coexist.

### 5. Map or predict

These are alternatives: frequency-ranked codes lose the smoothness prediction
needs.

**Map (dictionary).** A part-scope dictionary stored as a SeriesPack column.
Sorted dictionaries preserve order, so range predicates become code ranges and
code search can use Boundlist's endpoint `lower_bound` over the dictionary.
Frequency-ranked dictionaries give frequent values short codes but support only
equality. The dictionary is part-wide (**decided**), so codes are comparable
across the part's blocks; cross-part work needs translation (survey §5,
lesson 6). A block whose values the dictionary serves poorly opts out and
stores unmapped values. A later write of a value not in the dictionary becomes
an override; codes stay stable until compaction rebuilds the dictionary.

**Predict.** Residual `r_i = (u_i − pred_i) mod 2^64`, with predictor *none*,
*previous* (delta), *linear* (fixed-point, as in Calico's binding), or *category*
(the previous value in the same category). No zigzag is needed: stage 7 takes its
base as the minimum residual read as signed, and `(r − base) mod 2^64` is small
whenever the residual spread is.

Each sub-block of 16 starts from a checkpoint, so a point read costs one
checkpoint plus at most 15 residuals. Checkpoint spacing and storage are a
curated option Engine chooses (**decided**: Engine decides and Ikea exposes curated
options, unless one option is a single clear overall winner, in which case Ikea
just uses it; native or bit-packed every 16, every 64, or none, priced on Jena in
[the encoding inventory](encoding/README.md#jena-prediction-in-real-bodies)).
Checkpoints are stored relative to the block's first value, in the Relative
Ends style. Since `u_i = u_0 + i·base + Σ s_j`, a checkpoint need only hold the
cumulative stored residual `Σ s_j`. Without exceptions this fits `K + 8` bits,
about `0.06·(K + 8)` bits per value for 15 checkpoints. **Exceptions break that
bound**: an exception carries a full-width residual into every later
checkpoint (one patched delta of 2^40 in a K=1 block needs a 41-bit
checkpoint). Checkpoint width therefore depends on the block's exceptions, or
checkpoints exclude exception contributions and add them back from the
exception list; which is cheaper is open. A Sizes-style form (no intra-block
checkpoints, prefix sum on read) is the compact alternative.

For non-decreasing data with the *previous* predictor, this is exactly
Boundlist's geometry: residuals are sizes and checkpoints are group origins.
Boundlist's search then answers predicates on the column directly. Its
endpoint searches already accept wide custom ordered sources (`from_ends`);
only the compact *stored* view is offset-shaped (sizes ≤ 65,535, extent ≤ 256M).
Search over monotone 64-bit values therefore needs a source adapter, not a new
search law. General (non-monotone) prediction is not a Boundlist concern.

### 6. Block kind

- **Plain**: values packed at their full working-domain width, with no base
  and no exceptions. The universal fallback when compression is ineffective.
- **Constant**: the block stores only its value in the descriptor; no payload.
- **Runs**: run lengths as a compact Boundlist Sizes geometry (a block has at
  most 256 runs, Boundlist's exact limit) plus run values as a nested series
  that takes stages 7–8. A point read is `reverse(position)` then one value read.
- **Frame of reference**: the general case, stages 7–8.

Constant and runs are early exits, but they apply to whatever stream reaches
this stage: logical values, dictionary codes or the high part of a split.

### 7. Frame of reference and exceptions

`s_i = v_i − base`, packed at width K. Base and K are chosen over the values that
fit, from priors with fallback ([residual bounds note](../../notebook/pfor-bounds.md)),
scored on physical bytes, since SeriesPack tile geometry makes narrower not
always smaller.

Values that do not fit become **exceptions**: the packed slot holds 0 and the
exception list holds the stream value. Exceptions are encode-time, ordered and
immutable until the block is re-encoded. They live in the block body as a
Calico-style tight section: count, 8-bit block positions, values at their own
width. In predicted blocks the exception value *is* the residual, so the chain
stays intact (**decided**: patches come after delta).

### 8. Pack

A SeriesPack format per block, chosen from the preset policy for its width:
Local or Striped, optionally with a head. Placement within the group is the
block body offset from the group's Sizes geometry.

## Overrides: mutable outliers

**Overrides** are Ashton's outlier list with slack (**decided**: in-bounds writes
in place, others to the list, forced compaction when slack runs out). They differ
from exceptions in kind:

| | Exceptions | Overrides |
| --- | --- | --- |
| Created by | Encoding (width outliers only) | Writes that cannot land in place |
| Value domain | Stream value at stage 7 (feeds the chain) | Logical value, or null |
| Order | Sorted by position, regular | Arrive in any order |
| Storage | Block body, tight, immutable | Group store with slack |
| Read | Fills the packed slot before the inverse stages | Replaces the decoded result, never fed back into predictors |

Applying overrides only to outputs keeps predictors, checkpoints and exceptions
untouched by writes, so a write never re-encodes neighbours.

**Index.** Each block with overrides keeps a 256-bit membership bitmap. A
position's override is entry `popcount(bitmap below p)` in the block's array,
so entries stay sorted by position without searching. A new override inserts
into that small array.

**Storage and slack.** The group's override store is StreamPack-shaped: 16 ranks
(one per block), each with a reservation R and used length U over Boundlist
Sizes geometry, and group-level slack at the end. A block that fills its
reservation takes slack by rank-preserving repacking within the group. When the
group's slack is exhausted, the group compacts: its blocks re-encode with the
overrides folded in, and the store resets. Values are stored relative to the
block base at a width recorded per block and widened on demand.

## Writes

A write is logical: `(position, value or null)`. The procedure is a pure function
of the representation's bytes and the write, so it is deterministic:

1. If the position already has an override, update it.
2. Otherwise, if the block is a plain or frame-of-reference block (not
   constant, runs or predicted), the position has no exception, the column is
   dense and unrouted (or the write keeps its category), and the value passes
   stages 3–7 within the block's parameters (dictionary hit, round trip holds,
   residual fits K), write the packed slot in place. A constant or run block
   shares one stored value across rows, so changing one row is never a slot
   write. SeriesPack's replacement path provides the issued-byte journal.
   Its `sum_change` is a modulo-u64 delta in the *packed* domain. Under a
   fixed-population replacement and an affine integer map (plain, frame of
   reference, offset-binary signed), the bias cancels and it equals the
   logical delta modulo 2^64. Dictionary codes, splits, null-count changes,
   floats and wide or checked sums need their own contribution laws.
3. Otherwise, add an override. If that exhausts group slack, compact the group.

Null writes to dense columns update the null mask. Writes that change presence
in compacted columns become overrides carrying the presence change, since
inserting into a compacted series would shift its neighbours.

Removing an override when its value would now fit in place is left to
compaction, keeping writes monotone in the override store.

## Streaming

Streaming is the other place kinds mix (**decided**): data is collected in a
simple format and re-encoded one group at a time.

- **Collect.** The part's tail group takes appends as plain blocks: working
  domain values at full width, presence mask, and statistics maintained per
  append (min/max widen, count and sum accumulate). Appends need no analysis,
  no exceptions and no overrides, and read like any other plain block.
- **Re-encode.** When a tail group fills (or earlier, by Engine's choice), it is
  analysed and encoded with the part's expression, using priors from the
  groups already encoded. The new group replaces the plain one; logical
  contents are unchanged, so in Orbital's terms this is a physical
  representation change, not a new logical version.
- **One mechanism.** Streaming re-encode and forced compaction are the same
  operation on one group: encode its current logical contents (including
  overrides) into fresh blocks. Only the trigger differs.

A streaming part is therefore an encoded prefix plus a plain tail. The plain
tail is also what lets the part-wide dictionary grow: new values arriving in
the tail join the dictionary when their group re-encodes. Whether that changes
existing codes (sorted dictionaries) or only appends them (frequency or
insertion order) is open.

## Reads

A point read at position *p*: block `p >> 8`, then
1. override bitmap set → the override's value;
2. otherwise by kind: constant → value; runs → Boundlist reverse, then value
   read; frame of reference → SeriesPack `get` plus base, or the exception's
   value; predicted → checkpoint plus up to 15 residuals;
3. then inverse stages 5 → 3 (dictionary lookup, recombine split, inverse
   domain map), and presence.

Bulk reads decode a block's stream, apply exceptions while the residuals are in
registers (Calico's fused tier, survey §2), run inverse stages, then apply
overrides by iterating the bitmap.

### Separated filter heads (Ashton, 2026-10-22)

Frame carries SeriesPack's separated leading one- or two-byte planes (H = 8 or
16). They sit late enough in the pipeline to hold useful entropy, yet keep a
predicate meaning that doesn't need the suffix. Ikea LEAD's
[filter consumer](filter/README.md) owns the implementation. Which cut
satisfies both conditions depends on the stage:

- **After block frame of reference**, heads are the top bits of the K-bit
  residual. Entropy is high, but the head's meaning varies per block (base
  and K), so a threshold translates per block.
- **After a part-level frame of reference (before block FOR)** is a middle
  candidate: one interpretation across the part, so a contiguous head plane
  scans with a single threshold. It costs entropy where block bases differ.
- **Before prediction:** residual heads say nothing about values, so
  predicted blocks need value heads from an earlier cut, a separate summary,
  or a fallback.
- **Dictionaries:** a sorted dictionary's code heads are order-preserving
  (translate the threshold through the dictionary). A frequency dictionary's
  code head identifies the most frequent values, which answers equality on
  them without the suffix.
- **Exceptions:** exception slots hold residual 0, so their heads mislead;
  a head predicate must merge the exception list.

### Coarse masks

Ashton's requirement (2026-10-22): most operations accept a filter mask at
coarse granularity (for example one bit per block or group) and use it to skip
work and data movement. A coarse mask stays coarse. It decides which
descriptors, payloads, exceptions, override slices and statistics are touched
at all, and it composes separately with row masks inside the admitted blocks.
Coordinates and requested output order are preserved.

Skipping has dependency halos, which the encoding must declare:
- **Predicted blocks** need their predecessors only back to the nearest
  checkpoint; with block-start anchors, a block is self-contained.
- **Compacted presence and routing** need the rank prefix of skipped blocks
  (per-block counts, not their bodies) to locate admitted rows.
- **Dictionaries** are part-wide, so an admitted block needs the dictionary,
  but only entries its codes reference.
- **Overrides** are indexed per block, so skipped blocks' overrides are never
  read.

Grain and API stay open; QUESTIONS.md lists the experiments.

## Block descriptor and statistics

Each block has a fixed-size descriptor, a TuplePack record of byte-contained
codes: which part stages the block uses (domain variant, split, codes), kind,
predictor, SeriesPack format and K, exception count and width,
override width, split and ALP parameters. Wide values (base, constant value,
checkpoint origin) go in a parallel SeriesPack plane so descriptors stay small.

Block statistics are separate planes so they scan contiguously: present count,
min and max in the working domain (order-preserving, so comparisons are valid
there). Sums over 256-value blocks are not Frame's: they are stored on the
parent segment as summaries (**decided**), which Frame feeds with logical
contributions on writes (see the `sum_change` caveat under Writes) and through
block encoding. Any finer aggregates, such
as over 16-value sub-blocks, would stay local to the part; checkpoints are the
only such aggregate this design needs so far.
Statistics cover all rows including exceptions and
overrides, unlike frame-of-reference bounds, which cover only inliers. Writes
widen min/max immediately. A write that could shrink an extreme leaves the old
bound conservative: still safe for pruning, but no longer exact. Only exact
MIN/MAX answers need the exact data until compaction refreshes the bound.
Statistics may therefore carry a precision level (exact, conservative) rather
than a single dirty bit.

## What each stage preserves

A coarse first summary of the survey's matrix. Intersecting properties along
a path understates what encodings allow: a composition can introduce a new
algorithm (a frequency dictionary still answers range predicates by
classifying its entries, then scanning codes against that truth table). The
[review](REVIEW.md#1-preserve-structure-across-the-execution-boundary) treats
what cheaper problem each encoding creates as the real question; this table
is not intended as Frame's planning interface.

| Stage | Order | Linear sum | Positions | Point cost |
| --- | --- | --- | --- | --- |
| Presence dense / compacted | ✔ / ✔ | ✔ / ✔ | ✔ / rank | O(1) / rank |
| Routing | ✔ within route | ✔ per route | rank | rank |
| Offset binary, float map | ✔ | needs bias (ints) / ✗ (floats) | ✔ | O(1) |
| ALP decimal | ✔ within block | exact integer | ✔ | O(1) |
| Split | high part first | `2^s·Σhi + Σlo` | ✔ | two reads |
| Sorted / frequency dictionary | ✔ / via entry truth table | via code histogram | ✔ | O(1) + lookup |
| Predict | ✗ (except monotone) | telescoping only | ✔ | ≤ 15 steps |
| Constant, runs | ✔ | `v·n`, `Σ v·len` | ✔ | O(1) / search |
| Frame of reference | ✔ | `n·base + Σs` | ✔ | O(1) |

## Candidate presets

| Preset | Expression | Typical columns |
| --- | --- | --- |
| Plain | domain → FOR → pack | General integers |
| Sorted keys | domain → predict(previous) → FOR → pack | Keys, timestamps, monotone counters |
| Low cardinality | domain → sorted dictionary → runs or FOR → pack | Status codes, enums |
| Decimal float | ALP → FOR → pack | Prices, measurements |
| Precise float | float map → split(hi → dictionary, lo raw) → pack | High-precision doubles |
| Sparse | compacted presence → any of the above | Mostly-null columns |
| Routed | route by category → any of the above per route | Metrics keyed by type |

## Reuse of Ikea primitives

| Need | Primitive |
| --- | --- |
| Packed values at any width | SeriesPack formats and presets |
| Split values across planes | SeriesPack `value_expression` / substitution |
| In-place writes with journals and sum deltas | SeriesPack replacement path |
| Block descriptors | TuplePack records |
| Block bodies in a group; compacted-series offsets; run lengths; monotone deltas | Boundlist Sizes (and Relative Ends for checkpoints) |
| Override store with slack | StreamPack-shaped reservations over Boundlist |
| Null masks, override bitmaps, cold storage | Bec256; plain 32 B when hot |
| Dictionary search | Boundlist `lower_bound` over a SeriesPack endpoint source |
| Route-local rank | SeriesPack `filter<equal>` plus popcount |

Boundlist appears in four roles. Its existing endpoint sources cover wide
monotone values; whether the third consumer warrants a rename is Ashton's call,
not a prerequisite for prototyping.

## Open questions

Collected with the spike's other questions in [QUESTIONS.md](QUESTIONS.md).
