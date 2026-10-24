# Preparing Calico SUM from actual source cells

This bounded utility prepares the [native control](README.md) for the same
stored-domain forcing values as the [complete-query cells](../cells/README.md).
Their [findings](../cells/FINDINGS.md) retain small-query losses and shared-
capacity reversals. Calico's historical FastLanes and patch losses remain in
its source findings. This contribution supplies callable candidates and exact
functional checks, with no timing selector or performance-parity result.

## Callable boundary

Link `frame_calico_controls` and include [corpus_sum.h](corpus_sum.h):

```cpp
auto candidate = calico_control::prepare_sum(
    original_u64, declared_field_bytes,
    {frame::LayoutId::lane256, 256});
if (!candidate) {
    // Record candidate.refusal/detail as inapplicable; select no fallback here.
} else {
    auto query_ranges = candidate.owner->sum_ranges();
    auto retained = candidate.owner->retained_ranges();
    calico_control::u128 answer;
    candidate.owner->sum(answer); // consume and check in the caller
}
```

The returned owners stay at stable addresses. Original input is needed only
through preparation. The factory preserves the declared unsigned byte width
1/2/4/8 and refuses out-of-domain values. It never infers a narrower logical
field from a residual width.

Admitted programs are **lane256/256**, **byteplane/256**, and
**FastLanes/1024**. Other program/grain pairs are explicit study refusals,
including FastLanes/256; this is not a claim that every other source program
is impossible. Byteplane additionally refuses a complete frame requiring more
than its actual 16 residual bits. There is no low-plane truncation.

For each complete source frame the utility calls actual Engine `order_encode`,
finds its signed working-domain min/max, and creates the minimum-base,
minimum-bit-width covering box. The span is computed in i128, including full
u64 extrema. Exact `(base,bits)` pairs are deduplicated without merging.
The smallest legal source palette, 1/16/256, covers the distinct candidates;
more than 256 distinct boxes is refused. Unused symbols duplicate the first
candidate and are fully charged. Actual RawCodec selection chooses among
these published boxes; selected symbols are recorded as well.

The fitted Type goes to the existing `unsigned_image`: actual Type-bound
store, checked evidence admission, and an immutable borrowed query view.
Partial rows remain the source's literal tail. Empty or literal-only images
have one neutral Type symbol but zero fitted main boxes; they do not claim
compressed main frames. FastLanes groups four consecutive selected 256-row
blocks into each genuine 1024-row frame. Within the selected sequence no rows
are reordered. No aggregate is prepared or cached. Upstream SUM still does
its body walk, u64 signed-order correction, and literal-tail consumption.

## Source cells and applicability

All three inputs are the existing byte-identical `.u64` files referenced by the
cells panel. The runner verifies their full hashes before and after and reads
only source blocks `[0,1464,2929,4394,5859,7323,8788,10253]`, 2048 rows each.
These evenly spaced original blocks are forcing inputs, not population samples.
All three declared field widths remain **8 bytes**, including OS's stored
`2^63 + value` bias. Its actual Calico working values are small after upstream
order encoding; its query result still sums the original biased u64 values.
No new input acquisition is performed.

| Column | Program | Fitted distinct boxes / source symbols | Max residual bits | Actual container body bytes |
|---|---|---:|---:|---:|
| OS | lane256/256 | 6 / 16 | 8 | 1928 |
| OS | byteplane/256 | 6 / 16 | 8 | 1928 |
| OS | FastLanes/1024 | 2 / 16 | 8 | 1954 |
| UserID | lane256/256 | 8 / 16 | 64 | 13896 |
| UserID | byteplane/256 | Inapplicable: full residual width >16 | | |
| UserID | FastLanes/1024 | 2 / 16 | 64 | 16162 |
| URLHash | lane256/256 | 8 / 16 | 64 | 16520 |
| URLHash | byteplane/256 | Inapplicable: full residual width >16 | | |
| URLHash | FastLanes/1024 | 2 / 16 | 64 | 16418 |

Body bytes include actual evidence, main symbols/residuals and tail. Each
metadata image adds 24 bytes. The Type wire image is published separately.
These sizes are not timings, traffic or the complete retained footprint.

## Preparation and accounting

Preparation includes physical-domain validation, actual working-domain fitting,
Type construction, upstream plan construction, wire staging, store, independent
checked open and the borrowed open. Nothing needed by actual upstream SUM is
precomputed into a query answer. `sum_preparation` publishes frame/tail counts,
palette, max bits, Type wire size, temporary fitting capacity and source wire
staging bytes. The last two are separate temporary buffers, not an upstream
allocation-peak or total preparation-time measurement.

`retained_ranges()` exposes actual public owner/header, Type, binding, bound
SUM table, metadata/body and derived-view allocations with logical extents and
capacities. `sum_ranges()` exposes the sufficient actual query ranges. The
client verifies nonoverlapping retained charges and query-range containment.

The receipt separates potentially shareable **Type/program/binding/plan** bytes
from per-image body and view state. A conceptual component-header split is
included within already charged object headers; it adds no second header charge
and does not imply physical cache-line separation. Each real candidate currently
has 1189 known shareable logical bytes, plus 528 source-derived logical bytes
for RawCodec's private vectors. The latter's headers are already counted;
private capacities and allocator overhead are opaque. Public capacities are
reported exactly. Full allocation peaks are unmeasured.

Actual factory owners retain private copies of their entire interpretation
state. The conceptual shareable account does **not** subtract those bytes from
current actual ownership or pool allocations. It allows a later comparison to
price common schema/plan information separately from bodies, which remain
private replicas. This utility introduces no sharing machinery. Type wire files
are diagnostic exports, not another retained wire copy inside the query owner.

## Qualification and recovery

The existing [runner](run.py), using existing optimized/SAN caches on CPU8/j1,
builds `frame_calico_corpus_check` as a functional client. It acquires no corpus
and runs no benchmark. `--corpus DIRECTORY` can name another local copy with the
same frozen hashes. The [qualification](qualification.json) and
[evidence recovery](corpus-evidence-artifact.json) include every applicable and refused
real candidate, actual Type/meta/body exports and the called prepared-to-Engine
SUM/NEON path.

Both optimized and ASan/UBSan clients pass 283 synthetic cases, 279 accepted
constructions including seven real candidates, 19 synthetic refusals, two real
byteplane refusals, 1,639 actual SUM calls and 52,447 checks. All 21 Type/meta/body
exports match across builds. The independent read-only review found no defect;
it supplied no runtime evidence.

The synthetic matrix covers constants/nonzero bases, mixed frame widths,
physical widths, u64 extrema/high-bit crossing, empty and partial literal tails,
palettes 1/16/256 and 257-box refusal, repeated boxes, unsupported domains and
original-input lifetime. Independent C++ original-value u128 sums check every
actual query; Python independently re-reads the frozen real cells and checks
all real outcomes. Optimized and ASan/UBSan exports must match byte for byte.
The largest fixture has 76,800 rows, bounding its exact u64 SUM below `2^81`.
The prior extreme signed predicate overflow remains a separate expected
negative; this factory qualifies SUM and introduces no predicate claim.
