# A native length child inside BEC intersection/count

Keep this as a useful bounded composition result. Substituting the current
SeriesPack native six-bit length region broadly preserves the specialized
consumer's whole-operation performance, with smaller native endpoints in this
build. Against an ordinary byte-array materializer, the median whole-count
improvement is 3.3%, while the isolated refill improvement is 54%. The difference
in scale matters: a faster child does not imply a proportionate consumer gain.
No production reader or public interface changes are installed.

This follows the [performance and maintenance direction](../ikea-composition/design.md#performance-and-maintenance)
and the retired [striped boundary replacement](../seriespack-range-execution/striped-fragments.md).
This closes the bounded consumer probe. Primitive coverage and the overall
performance judgment remain part of the broader SeriesPack delivery campaign.
It does not establish general recovery of a primitive performance deficit, or
a universal tolerance for one.

## The substitution and its scope

The [adapter](README.md) uses the closed heterogeneous probe's
scan128 metadata and BEC intersection/count operation. All three readers share
the same population packets, checkpoints, dense body allocation, query bitset,
prefix/address reconstruction and authored `count_range`. BEC bodies retain
their existing 64-readable-byte suffix admission. Both existing inline/split
body-provider cuts are measured.

The specialized control is the old metadata provider in the new common
`Source`/`Cursor` wrapper, not a byte-identical historical whole-pipeline
baseline. Native uses the current private SeriesPack `striped<6,16>` region;
materialized uses the ordinary bound reader into `u8[16]`, then the same native
prefix consumer. No widened descriptor array is imposed on that control.
Standalone refill timings include a common `u32[16]` store sink. Whole count
retains native metadata lanes and does not construct that array.

`Source` retains the actual metadata, body and query owners and stores its views
by value. The SeriesPack child's logical length N excludes initialized scan128
capacity C; its payload span is precisely the owned length plane, with a
96-byte stride between 128-value tiles. A refill begins at the 16-record
checkpoint, including preceding lengths needed to establish the exclusive
prefix. The materializer decodes through `min(group+16,N)` and zeroes only its
unused final scratch suffix, inside measured work. Native may read initialized
physical slack, which remains inactive. Query/body ordinals stay original, and
the first scalar entry survives a refill for the second body of a pair.

## Measured result

Zen job `20260911T042644Z-8d76720a` uses capture
`f916f67c86a1ee1996b6a9d0019814877c1b687b6bb9051a17aa72fbdb3b3c50`,
Clang 21.1.8 and release `-O3 -g` with assertions retained. The feature ceiling
is x86-64-v4 plus VBMI, VBMI2, GFNI, VPOPCNTDQ and BITALG, tuned for Zen5.
The AVX2-labelled materializer therefore runs inside a full-feature BEC build;
this is not a pure AVX2 deployment comparison.

The [target screen](evidence/bec-metadata-20260911/20260911T042644Z-8d76720a/target-screen.json)
finds AVX2 and AVX512 effectively tied across nine refill contexts: per-context
median ratios range 0.9978–1.0044. The predefined median-ratio rule selects
AVX2 at 0.999669. This supplies a reproducible, refill-screened global control,
not an ISA preference or the fastest target for every whole-count range.

Fifteen workloads each contain eight windows: all twelve existing RealRoaring
archives, structural and random-half 256-block windows, and a structural
129-block tail. Empty/full cells remain present. Six ranges cover full work,
nonzero starts, checkpoint crossings at 15 and 127, and the last block.
Three standalone refill positions include the final logical group.

The full comparison runs in one process with identical allocations across
specialized/native/materialized/materialized/native/specialized blocks. CPU0,
cases and repetitions are sequential; collection sync is disabled. Each block
has three 30ms repetitions. Each benchmark iteration performs 256 operations
over eight windows. The [samples](../ikea-composition/archive/validation-20260911.md)
record footprint, first-window addresses modulo4096, original ranges and
logical counters. These are resident reused windows, not a cache-size sweep.

| Native compared with | Operation | Cases | Separated wins / losses / overlaps | Median native/control |
| --- | --- | ---: | ---: | ---: |
| Specialized | Refill plus common sink | 45 | 45 / 0 / 0 | 0.9711 |
| Specialized | Whole count | 180 | 52 / 13 / 115 | 0.9993 |
| Materialized u8 | Refill plus common sink | 45 | 45 / 0 / 0 | 0.4570 |
| Materialized u8 | Whole count | 180 | 175 / 0 / 5 | 0.9669 |

“Separated” compares the extrema of all six samples per reader, not a confidence
interval. The [paired table](evidence/bec-metadata-20260911/20260911T042644Z-8d76720a/paired.csv)
keeps every case and both ordering edges. Whole-count edges agree in 124/180
specialized comparisons and 177/180 materialized comparisons. All thirteen
specialized losses are in the inline cut; the worst is random-half `127/2`,
66.431→67.159 ns, or 1.10%. All specialized whole-count median ratios lie
between 0.9861 and 1.0110. This supports preserving performance, not a broad win.

Across refill cases, median costs are 2.087 ns native, 2.127 ns specialized and
4.545 ns materialized. For whole 256-block counts, the median native/materialized
ratio is 0.9826; checkpoint-crossing two-body ranges are about 0.959. The partial
129-block source is different: its final single block takes 56.635 vs98.135 ns
in the split cut, and 56.655 vs94.448 ns inline. Its `127/2` crossing saves about
28%. Those larger tail benefits remain explicitly confined to that case.
The [shape summary](evidence/bec-metadata-20260911/20260911T042644Z-8d76720a/review/shape-summary.json)
retains the distinction. Body work diluting refill savings is an interpretation
of these whole operations; no timing subtraction estimates a separate body cost.

## Authoring, code cost and verification

The adapter is 139 source lines including ownership/admission and all three
controls. It reuses existing physical reads and native prefix/body functions.
Actual worker endpoint sizes are:

| Endpoint | Specialized | Native | Materialized |
| --- | ---: | ---: | ---: |
| Refill plus common sink | 304 B | 284 B | 297 B |
| Inline whole count | 4,046 B | 4,002 B | 3,957 B |
| Split whole count | 1,258 B | 1,213 B | 1,262 B |

These are [endpoint sizes](evidence/bec-metadata-20260911/20260911T042644Z-8d76720a/review/codegen.json),
not whole-library savings. Specialized/native inline counts have no calls or
SIMD stack accesses. The materialized inline endpoint has three static bound
decode call sites and three conditional final-tail `memset` sites; that is not
six calls on every query. Its out-of-line decoder is excluded from endpoint size.

Release and ASan/UBSan checks each pass 254,370 compound ranges and 17,040
metadata entries, including logical tails, admission, owner/view moves and
poisoned unrequested query blocks. The worker also passes existing bitset,
integer and heterogeneous checks, plus 3,737,352 exact guarded SeriesPack ranges
for each of scalar, AVX2 and AVX512. The [independent audit](evidence/bec-metadata-20260911/20260911T042644Z-8d76720a/review/audit.json)
checks both captured source archives, 42 compiled sources, 35 archive members,
seven link inputs, 216 target-screen samples, 1,350 preflights and 4,050 timings.
Independent Python reconstruction matches all fifteen input hashes and ninety
query-count contexts. The [first attempt](evidence/bec-metadata-20260911/20260911T042644Z-8d76720a/review/initial-build-failure.json)
omitted BITALG and stopped during compilation; it produced no timing result.

This gives the [retained-plan questions](../../notebook/ideas.md#plans-that-keep-improving)
a concrete authoring lesson: the native length operand, metadata checkpoint
grain, physical tile and body-pair consumer need not share a width. The enclosing
cursor supplies predecessor dependencies and retains useful native state while
the child preserves its own owner, placement and logical identity. This is a
known-geometry substitution; schema-driven discovery and arbitrary nested
rewrites remain separate questions. Keep the small example and the measured
tradeoff, without promoting a universal cursor interface from this one consumer.
