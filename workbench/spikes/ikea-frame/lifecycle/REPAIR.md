# Fitting writes, sparse proofs and row-selected reads

Use sparse value proof before re-encoding a raw packed/delta block. Keep changed
block folding as the fallback, and retain the immutable-base/override program for
run-heavy inputs. This improves the write surface materially; it does not select
one universal Frame policy or make compressed hot reads uniformly faster than
native arrays.

The [block-fold cohort](BLOCK-FOLD.md) removed unnecessary codec work on sparse
edits, but still decoded every dirty block. Workbench's read-only review identified
the next discriminators: separate dirty-block eligibility from a failed fit,
charge native batch coalescing, expose hidden NULL fill, and test a later window
chosen before timing. The resulting probe keeps the existing controls and adds
sparse repair, native coalescing and Claude's row-selected aggregate programs.

## Correctness and physical authority

For delta value changes `d_i = new_i - old_i`, residual repair is
`s'_i = s_i + d_i - d_(i-1)` for i>=1. The descriptor anchor becomes `new_0`;
a checkpoint through j changes by `d_j - d_0`. Thus an anchor edit can repair
many checkpoints instead of forcing a rebuild. Every new residual must fit its
existing width before any destination store. Wide stored prefixes may wrap
modulo u64; logical values and exact u128 reductions do not wrap.

Dense repair reconstructs/proves a whole block. Sparse repair reads old points
only for final assigned rows, stages residual events at those rows/neighbours,
and repairs checkpoint prefixes. It maintains conservative bounds like the
native control rather than rescanning all values for exact extrema. Unsupported
kinds, coded blocks and nonfitting values fall back to existing block folding.
Flags-only/metadata-only changes can retain the value Part.

The scalar setter uses supported SeriesPack point operations on a fresh,
initialized and isolated copy. `view::assume_valid` reuses its existing placement
proof. This grants no authority over retained bytes and emits no effects or
summaries; in-place owner mutation would still require source-qualified effects
and isolation over all actually issued stores. The full group copy remains an
explicit physical envelope and cost.

Dense and sparse planners pass 11,264 plans / 3,108,864 independent points and
aggregates over all 64 widths, four checkpoint modes, anchors/interiors/last rows,
whole descriptor shifts, wide checkpoint wrap, refusals and unaligned guards.
Native owner checks add 328,160 comparisons over raw/sorted/frequency dictionaries,
actual fitting anchor/CP changes, fallback, preserved dictionary dependencies and
old readers. Native coalescing separately proves latest-assignment semantics,
publication even for a visible no-op, no widening from discarded intermediates,
flags-only reuse and retained reads.

## Fix the sample interpretation

The old first 8192 EventTime values encode as 32 run blocks. Twenty predeclared,
aligned windows over that export instead have median 30.5/32 packed-or-delta
blocks. UserID stays run-heavy (prefix 32 runs; sampled eligible median 0/max 1).
Pressure is mostly delta throughout. Therefore no fitting successes in the old
EventTime prefix was absence of eligible kinds, not a residual-fit failure or a
representative result for EventTime.

The new bounded probe retains pressure's prefix and takes EventTime's exact
aligned midpoint, row 1,495,040, chosen before timing. Input identities record
original/slice hashes, offsets, threshold and initial predicate hit fraction.
The threshold is still initial row 71, not a claim of a 50% predicate. Prefixes
remain named forcing cases; no population weights are inferred from these slices.

Both windows contain 8192 values in two 4096-row groups. Eight batches each have
64 ordered assignments. The probe deliberately limits the earlier chronology to
repeated-hot/dispersed edits, all original blocks and no retained old reader.
It compares value-only and mixed NULL/presence traces, one query/publication or
64/publication plus 512 final queries, and masked versus decode-reference query
programs. Other masks, reader ages, novelty and run-heavy prefixes remain in the
earlier full cohorts; they have not disappeared from the research surface.

## Complete update phase

Mixed trace, masked query program, median µs per complete eight-publication phase:

| Input / edits | Native FOR | Coalesced native | Dense repair | Sparse repair | Overrides, no fold |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pressure / repeated-hot | 57.3 | 60.9 | 82.1 | 77.6 | 63.6 |
| Pressure / dispersed | 86.2 | 99.5 | 311.4 | 182.2 | 167.8 |
| EventTime midpoint / repeated-hot | 55.0 | 56.6 | 78.3 | 74.3 | 67.1 |
| EventTime midpoint / dispersed | 86.2 | 98.0 | 208.2 | 111.5 | 156.8 |

Sparse proof removes most examined work. On pressure/dispersed, dense repair
materialises 56,064 positions and examines 45,312; sparse repair materialises 256
positions and examines 463. Both still copy 84,832 bytes. Dirty exposure is 219
eligible blocks: sparse reasons are 176 successes, 42 no-value changes and one
residual-bound rejection. EventTime’s 219 dirty blocks are packed: 177 successes,
42 no-value changes, no rebuilds. These counters describe logical work, not
issued loads or cache traffic.

The coalesced native arm charges sort/allocation, keeps the last input ordinal,
checks visible equality and preserves all eight publications. The repeated-hot
case drops 448 intermediate commands and detects 10 final visible no-ops, but its
extra preparation does not improve this complete chronology. The earlier native
control weakness was worth testing; it did not explain the large writer gaps.
Keep both controls and use the stronger observed one for decisions.

Native FOR still wins updates: sparse pressure/dispersed is about 2.1× its time;
EventTime midpoint is about 1.3×. Eliminating re-encoding did not eliminate
planning, group copying and ownership cost. The remaining write questions are
batch indexing/coalescing, copy grain and sustained override debt—not another
scalar setter rewrite.

## NULL-aware reads change the comparison

The coherent owner cut uses Claude's row-selected stored/coded SUM/COUNT. The
all-valid path keeps the existing complete operation; mixed validity now uses
encoded algebra. The reference arm retains one decode followed by both
reductions, rather than charging two independent decodes.

Dispersed read-rich phases, median ms including updates and demanded queries:

| Input / trace | Native FOR | Sparse + decode reference | Sparse + masked algebra |
| --- | ---: | ---: | ---: |
| Pressure / mixed | 5.53 | 11.31 | 9.88 |
| EventTime midpoint / mixed | 5.04 | 9.30 | 3.65 |
| Pressure / values-only | 2.91 | — | 3.97 |
| EventTime midpoint / values-only | 2.39 | — | 4.65 |

Masked algebra removes about 61% of the EventTime mixed phase, taking it ahead of
native for that workload. Native remains faster on the all-valid window. The
mixed trace also makes native reductions pay per-row validity handling; it is
incorrect to generalise this win to all hot scans. Full packed SUM and COUNT
currently traverse/project separately, while the native control fuses them;
complete-operation fusion is another explicit remaining program choice.

Hidden NULL payload is not a logical value contract. Native reconstruction
zero-fills invalid slots and chooses widths over valid values; encoded folding
preserves its last contributing physical value; overrides retain the old base.
Sparse final-assignment proof can discard an intermediate value whose final
state is NULL. These are valid, different physical policies. Predictor-consistent
fill is Claude's next separate cut; do not attribute all resulting morphology
or timing differences to publication alone.

## Qualification, scope and recovery

The integrated core is immutable encoding 2c3bdf87 plus Ikea 2ce62539, not live
NULL-fill edits. Source capture:
`7b30bb0abae22cd02e55fba0fb389a4134861fbc04c3ae236e7f51f459a6451c`.
The material check passes 90,762 cases, including both masked and reference
selection programs, cached dictionaries/RHS changes and native retained owners.
The bounded trace adds 14,155,776 original-coordinate point checks and 9,216 timed
histories with exact demanded aggregates and native admission. All final owner
metadata/Arena charges return to zero.

Twelve arms include exact native/fold duplicates, balanced position/predecessor
rounds and 24 samples/cell. Duplicate ratios span .980–1.045 (median 1.000).
Interpret comparisons within this frozen cohort. The broad preceding dense-fit
cohort (source 64bdca, three prefix slices) is kept as historical forcing evidence,
not pooled with the changed source, controls or window here.

Local pinned Clang 21.1.8/O3/native/generic AArch64 VM only. Per-trace maximum
update/transition/release calls include the outer function's destruction; they
are diagnostic samples, not production tail qualification. Component clocks
are not an exhaustive decomposition of total time. Old-reader timing in earlier
cohorts includes extra old-image queries. Setup and final current-object disposal
stay outside phase clocks; native nonidle capacity excludes codec temporaries,
query scratch and RSS. Same-version adoption still retains its primary.

[All bounded cells, work/rejection counters and calibration](evidence/sparse-masked-local/)
and the recovery archive retain the losing cases, exact source/binaries, input
receipts and commands. The sampled morphology and earlier full forcing cohorts
remain discoverable. No Frame public API/default/graduation is selected; sustained
cycles/reader ages, native fleet, staged cold/Loom, wider operation families and
coarse-masked movement/backing grain still need their own evidence.
