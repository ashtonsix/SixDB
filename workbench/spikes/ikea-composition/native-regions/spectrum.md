# Grouped composition across local widths 9–32

The integrated grouped/deferred option has the lowest median in all 48
width/selection groups on both machines. GNR has clear measured margins;
Zen's all-active widths 18 and 25 overlap the fastest materialized control's
sample ranges and should be treated as ties. This supports a reusable execution
option across the two natural lane families, not a universal dispatch policy.

This extends the earlier [two-width probe and curation](findings.md#gnr-grouped-read-comparison)
using the current ordinary bound decoder as the materialization control. The
separate [arbitrary-range materializer](../../seriespack-range-execution/expressions.md) is not
integrated into this baseline. Its traversal regressions remain separate work.

## Comparison and scope

Each target runs 384 cases: every headless local width 9–32, all-active and 75%
selection, and eight plans. AVX2 supplies tile/immediate, tile/deferred and
ordinary materialization followed by array comparison/reduction. AVX512 supplies
those three plus grouped/immediate and grouped/deferred. AVX512 uses the
integrated native deferred type; AVX2 retains the experimental carrier control.
Existing direct controls remain available outside this focused sweep.

All plans process 8,192 original positions from the same deterministic fixture.
Materialization writes the smallest sufficient unsigned array and then runs
the existing compiler-vectorizable compare/sum loop. Decode, scratch reads and
writes, comparison and reduction are timed; allocation, attachment, binding and
the independent oracle are excluded. Both active and selected counts agree.
The 75% selection omits every fourth position; it is not a sparse or empty-group
workload. The timed sources are dense, with complete logical groups.

One immutable source capture, `3c7cdd61be10c2efc11bd6dfa6d95c6fdf9983c17ab0e8c892d27489608adb11`,
serves both machines. Both use pinned Clang 21.1.8, the full AVX512VBMI/VBMI2/GFNI
profile, target-specific tuning and CPU 0. The AVX2/AVX512 names identify
implementation families within that feature ceiling. Each case has three
sequential repetitions with a 0.1-second minimum. Actual benchmark inventories
match all 384 expected names; all 48 width/mask groups have matching logical,
active, selected and encoded work counters.

## Results

Ratios compare the grouped/deferred median with the fastest matching alternative
across both implementation families; less than one favors grouped execution.
All measured grouped winners use deferred finalization.

| Target | Grouped / fastest materialized | Grouped / fastest tile | Grouped sample range below materialized range |
| --- | ---: | ---: | ---: |
| GNR | 0.313–0.743 | 0.433–0.884 | 48 / 48 |
| Zen | 0.544–0.999 | 0.267–0.666 | 46 / 48 |

Zen width 18/all takes 0.09394 versus 0.09506 ns/value for the fastest
materializer; width 25/all takes 0.09689 versus 0.09699. Their three-sample ranges
overlap. A lower median alone does not establish an advantage there. Sample
extrema are not confidence intervals, and this is one linked program per target.

The result strengthens the distinction between a physical packet, its execution
grain and reduction finalization. The grouped executor retains each actual
child's source and stride, and the deferred result owns one modulo-u64 sum.
Neither requires a decoded intermediary or a new erased/continuation ABI.
For this workload the strongest tile/deferred alternatives also lose, so the
comparison does not attribute all benefit to removing scalar finalization.

The all-active Zen ties are the marginal cases to carry forward. They do not
justify a new format-specific implementation or selector by themselves; the
two existing grouping mechanisms already serve the complete width families.
Other extents, strides, masks, projections and consumers still need their own
comparison. This does not discharge physical codec, point/range, or CPS costs.

## Retained evidence

GNR job `20260910T211732Z-57e792b1` and Zen job
`20260910T211922Z-d92891c6` each pass 32,256 grouped guard/source cases and
224,490 deferred-result checks. The guarded checks cover independent sources,
strides and logical bounds; those placements are correctness evidence, not
timed stride comparisons. Every file in both 566-file source archives was
checked against the identical manifest, together with the retained library,
composition object and named implementation inputs.

| Target | Cases and all repetitions | Per-group alternatives | Sources, checks and limits | Recovery |
| --- | --- | --- | --- | --- |
| GNR | [cases](evidence/grouped-spectrum-20260910/gnr/cases.csv) | [comparison](evidence/grouped-spectrum-20260910/gnr/comparison.json) | [run](evidence/grouped-spectrum-20260910/gnr/run-facts.json) | [artifact](evidence/grouped-spectrum-20260910/gnr/artifact.json) |
| Zen | [cases](evidence/grouped-spectrum-20260910/zen/cases.csv) | [comparison](evidence/grouped-spectrum-20260910/zen/comparison.json) | [run](evidence/grouped-spectrum-20260910/zen/run-facts.json) | [artifact](evidence/grouped-spectrum-20260910/zen/artifact.json) |

The bundles retain the measured binaries, library, composition object, compile
commands, disassembly, exact inventory/filter and complete source capture.
The selected source includes the spectrum overlay and worker verification
script; it does not depend on an uncommitted live benchmark edit for recovery.
