# Compact row signatures: first resident study

Concluded 2026-09-08. **Joint block summaries
and prefilters for expensive text checks have useful operating regions;
compact signatures are not a default win over cheap resident columns.**
Equal information in narrower planes does not by itself make execution faster.
The wider SixDB representation and planner choices remain open. This closes
the spike with an executable comparison and conditional findings; it does not
adopt a production design. Further work should start from a concrete downstream
cost or maintenance workload that could change these choices.

These are warmed, pinned **local ARM Linux VM** measurements, using Clang
21.1.8, C++23, Google Benchmark 1.9.4, and NEON. They are not Zen 5 results or
physical cache/bandwidth measurements. Each timing below is a median of five
sequential repetitions, in ns per input row; individual samples and min–max
ranges are retained. Scans write the same complete bitmap and return count
and row-ID sum, including exact residual verification. Construction and
replacement have separate measurements.

The [probe](probe.md) defines implementations, generators, timing boundaries,
and reproduction commands. The [evidence index](evidence/local-arm-20260908/README.md)
maps five retained runs, 354 comparisons, and 1,770 individual repetitions to
their captured sources. Results are deliberately separated by query, data,
hash, ordering, and source version rather than pooled into a single score.

## What the methodological feedback changed

The **Scaffold SixDB project docs** task was consulted before execution. Its
feedback favored cheap iteration, credible alternatives, and experiments that
change the next question. The opening experiment list became a menu, not a
sequence of required gates.

Two controls materially changed the results. Fusing same-plane conjunctions
and preparing masks once per scan removed avoidable signature-executor work.
A direct native range comparator then overturned an apparent progressive-range
win against a control that had retained unnecessary refinement bookkeeping.
Separately, a flat scanner's outer 16-row block loop made rollups appear faster
even when they skipped no blocks. Removing that redundant flat-loop handoff
made the block-skipping comparison meaningful. The earlier timing screens are
superseded; the retained confirmation runs include these corrections.

The next contrasts followed actual uncertainties: a second hash exposed
persistent repeated-value collisions; contiguous summary planes isolated
placement from summary information; a separate data seed checked the absent
conjunction; and long strings tested the encouraging short-text result.
No hardware-counter explanation is claimed from logical work counts.

## Rollup can beat the direct scan, if it preserves useful joint evidence

Eight scalar fields each have a four-bit hash tag. All dedicated-tag layouts
store the same 32 bits per row. Each joint rollup maintains a 256-bit set of
observed codes for **each of four byte planes**, preserving the two tags'
co-occurrence within that plane. It rejects a block when no observed code can
satisfy the query's necessary condition for a consulted plane.

For the absent two-field conjunction, both queried tags occupy the first
byte. At 1,048,576 rows, the signature admits 4,101 false candidates. The
[layout run](evidence/local-arm-20260908/rollup-layout/summary.md) holds those
candidates and summary information fixed:

| Path | ns/row | Blocks rejected | Auxiliary B/row, including row tags |
| --- | ---: | ---: | ---: |
| Exact columns | 0.495 | — | 0 |
| Flat byte planes | 0.572 | — | 4 |
| Joint 16, interleaved / planar | 0.600 / 0.432 | 61,535 / 65,536 (93.9%) | 12 |
| Joint 64, interleaved / planar | 0.331 / 0.320 | 12,720 / 16,384 (77.6%) | 6 |
| Joint 256, interleaved / planar | 0.446 / 0.440 | 1,490 / 4,096 (36.4%) | 4.5 |

The 64-row planar arm is about 1.55× faster than exact columns here. Sixteen
rows reject more blocks but require four times as many summaries as 64;
256 rows save summary space but admit many more blocks. Planar placement
substantially improves the 16-row arm at this population size. At 65,536 rows,
that particular placement advantage disappears (0.352 interleaved versus
0.358 planar); 64 remains the fastest tested size. This is an operating region,
not a universal best block size.

The conclusion survives four query targets and independently changed hash or
data seeds. With hash 71 and unchanged data, interleaved joint64 takes
0.311–0.322 ns/row across queries versus 0.492–0.495 for exact columns.
With data seed 93 and hash 13, planar joint64 takes 0.312–0.324 versus
0.494–0.501. These are ranges of per-query medians, not pooled repetitions.

Cheaper summaries retain less evidence. In the original confirmation's q0,
eight marginal tag-presence maps per 64-row block reject only 578 of 16,384
blocks, versus 12,720 for the joint sets. OR rollup of a shared 32-bit signature
rejects none. Flat shared 8/16/32-bit row masks admit 753,972 / 170,550 / 21,594
candidates, versus 4,101 for dedicated tags. All eight fields contribute to
each shared mask, so unrelated features consume its capacity.

**Repeated values make collisions persistent.** The `exclusive` and `paired`
fixtures have identical marginals but different within-row co-occurrence.
For exclusive rows under hash 13, q1–q3 have no signature candidates and
joint64 rejects every block, taking about 0.053 ns/row. But q0 collides with
a repeated nonmatching value: 131,072 rows survive and no block is rejected,
taking 2.056 ns/row. With hash 71 all four queries reject every block, around
0.054–0.055 ns/row. Marginals cannot distinguish the exclusive combinations.
Do not model repetitions of one colliding value as fresh independent trials,
or hide this failure by averaging the four queries together.

## Plane width depends on Boolean placement and the work left behind

The [confirmation](evidence/local-arm-20260908/confirmation/summary.md) gives
these equal-information comparisons at 1,048,576 rows, query zero:

| Query | Exact columns | One 32-bit plane | Two 16-bit planes | Four 8-bit planes |
| --- | ---: | ---: | ---: | ---: |
| One absent cheap predicate | 0.371 | 3.711 | 3.351 | 3.044 |
| Conjunction with first atom always true | 0.495 | 3.919 | 3.896 | 3.340 |
| Rare conjunction, fields 0 and 7 | 0.494 | 0.598 | 1.033 | 0.937 |
| `(A OR C) AND (B OR D)` | 0.745 | 1.849 | 1.532 | 2.447 |
| Hot conjunction, 81.1% actual matches | 3.054 | 3.705 | 3.685 | 3.717 |

A failed four-bit atom leaves about one row in 16 unresolved. A second atom
that is already true supplies no additional rejection; sparse exact checks
of those survivors are much more costly than the direct column control here.
There is substantial repetition dispersion among the slow single/near-match
signature arms; their precise internal ranking is not a finding.

For fields 0 and 7, progression skips the second byte plane for 34.9% of
16-row groups. Nevertheless, eager byte planes take **0.684** ns/row versus
**0.937** for progressive bytes. Reading less logical data loses in this
resident kernel. The result repeats under the second hash and the later
layout run. Control and mask bookkeeping are plausible contributors; they
were not isolated with PMU measurements.

For CNF, the natural byte grouping contains A/B then C/D, so an unresolved
other plane prevents either first-plane OR clause from disproving the query.
Putting A/C in one byte and B/D in the next avoids 7,978 of 65,536 second-plane
groups (12.2%). The later run measures 2.066 ns/row reordered versus 2.143
natural, with overlapping repetition ranges. The work reduction is clear;
its modest timing advantage does not establish a layout recommendation.
A single 16-bit plane containing all four tags is faster at 1.510, and exact
columns remain faster at 0.749.

The [OR follow-up](evidence/local-arm-20260908/data-and-or/summary.md) tests
`(A AND B) OR C` directly. The rare case has 2,588 actual matches but 71,490
signature candidates: exact columns take 0.667 ns/row versus 3.760–4.145
for flat signatures. For independent per-plane rollups, projecting away C
from the A/B plane, or A/B from the C plane, makes each necessary condition
tautological. The rollup cannot reject any block. This is a loss caused by
Boolean decomposition, beyond ordinary hash saturation. A broad C branch
adds a true-match floor; that case also favors direct columns.

Keeping the same rows but sorting the `broad` fixture by its queried fields
holds 5,889 true matches and 14,797 signature candidates fixed. Later byte
groups fall from 56,968 to 34,505; joint64 block rejection rises from 12 to
4,151. Placement changes work and time without changing row-level precision.
Exact columns still win in both arrangements.

For now, preserve 8/16/32-bit options in investigation code. Eight-bit planes
are candidates when they omit irrelevant information or settle whole fragments
early; 16/32-bit planes can combine useful evidence with less stage machinery.
Four byte planes still occupy four bytes per row. Width selection needs both
survivor cost and group-level continuation statistics, plus Boolean locality.

## Ordered prefixes save logical reads, but did not win this range scan

Here byte/halfword planes hold the **complete value in an alternative layout**,
not an auxiliary probabilistic index. A global unsigned interval query selects
about half the rows; leading planes can prove both acceptance and rejection.

| Range fixture, confirmation run | Native uint32 | Two 16-bit planes | Four 8-bit planes |
| --- | ---: | ---: | ---: |
| Uniform values | 2.065 | 2.357 | 2.432 |
| Shared first three bytes | 2.075 | 2.552 | 2.777 |
| Same uniform values, sorted | 1.247 | 1.389 | 1.363 |

The uniform byte path reads a logical 1.118 bytes per input row instead of
four: plane visits are 65,536 / 7,735 / 27 / 1 groups. Tied prefixes force all
four planes. Sorting reduces visits further, but the native comparator is
still faster, also in the later run. These measurements leave compressed or
cold-value refinement open; they do not establish that resident byte slicing
beats a good native comparator.

## Text needs enough capacity, or an exact cached predicate

The [text run](evidence/local-arm-20260908/text/summary.md) uses 65,536 unchanged
accident descriptions. All substring semantics are case-sensitive bytes and
all signature candidates are verified with the same `std::string::find`.

| Query | True rows | Candidates, 32 / 256 bits | Direct ns/row | 32-bit ns/row | 256-bit ns/row |
| --- | ---: | ---: | ---: | ---: | ---: |
| `Accident` | 23,657 | 61,741 / 26,554 | 29.131 | 29.221 | 14.043 |
| `lane blocked` | 3,575 | 41,860 / 3,852 | 27.256 | 22.813 | 3.195 |
| `construction` | 2 | 38,244 / 136 | 24.695 | 18.584 | 1.391 |
| `unobtainium` | 0 | 42,774 / 14 | 20.282 | 16.877 | 1.321 |

Eight-bit signatures pass essentially every row; 16 bits are also mostly
saturated. The 256-bit arm uses 32 bytes per row and a different wide,
row-oriented kernel; this is not a pure SIMD lane-width experiment. Direct
absent-query timing has an outlier, so the recorded min–max remains available.

On synthetic 512-byte strings, 32-bit signatures pass **every row**. The
256-bit arm still passes 72–83% of rows. It helps the expensive lowercase
searches (about 213–221 versus 287–297 ns/row), but loses for `Accident`
(19.673 versus 16.540): uppercase A is normally absent from the lowercase
generator and the direct search rejects cheaply. Nominal string length and
selectivity do not fully specify verification cost.

Four exact materialized term bitmaps use 0.5 bytes per row in total, and
answer these four real-text queries in 0.027–0.308 ns/row. Building all four
costs 100.134 ns/row, compared with 87.155 for the 32-bit gram signature and
166.178 for 256 bits. This is an attractive control for a stable, frequently
queried vocabulary; new arbitrary terms need new exact metadata. Frequency
of querying a term and frequency of that term in the data are different inputs
to the choice. Dictionary-level evaluation and inverted/positional indexes
were not implemented in this study.

## Construction, updates, and changing quantiles

Scalar tag construction costs 6.960–7.685 ns/row; shared32 costs 17.334.
The random field-replacement trace includes the primary field write: direct
AoS takes 5.017 ns/write, dedicated tags 8.483–8.807, and recomputing shared32
48.528. Both XOR replay passes are charged, restoring the initial state.
These are narrow single-thread maintenance costs, not insert/delete or a
storage engine's complete write path.

Planar joint64 construction on the absent fixture costs 9.572 ns/row.
Against the same run's 0.495 direct scan and 0.320 rollup scan, building this
metadata from scratch needs roughly **55 repeated full scans** to repay
construction alone. That arithmetic assumes static data and those warmed
query costs. Rollup repair, stale deletion accumulation, visibility, and
query diversity are unpriced, so it is not a sustainable-throughput claim.

The independent semantic model checks quantile drift. With old boundaries
25/50/75, shifting values from 0–99 to 100–199 leaves all 100 rows unresolved
for `x > 150`. Rebuilding at 125/150/175 reduces unresolved rows to 25 while
retaining the exact 49 matches. Frozen boundaries remain sound but can lose
usefulness; applying new boundaries to old codes can incorrectly certify a
row. Boundary editions and data/signature visibility belong to the semantics,
not just maintenance tuning.

## Reasons to return

The most useful next step is to put the promising summaries in front of an
actual expensive downstream path—compressed strings, dictionary decoding,
wide payload fetches, or sparse random candidates—and compare against that
path's best direct executor. This would distinguish avoiding bytes or decoding
from the resident comparator costs measured here. A mixed cheap-and-expensive
Boolean query would also test whether progressive evidence reduces actual
residual obligations, rather than only producing a candidate mask.

For row tags, vary precision and allocation by field, including wider tags for
repeated near matches. The present equal-information comparison fixes four
bits per field; it does not optimize the information budget. For rollups,
compare selective plane coverage and joint cross-plane evidence, then price
replacement/deletion repair and snapshot visibility before recommending a
maintained hierarchy. A query's necessary predicate over a plane and the
block's ability to retain a same-row witness are separate design choices.

For planning, expose evidence precision, whole-fragment coverage, survivor and
continuation rates, residual costs, physical grouping, and edition/coverage
validity. Keep bypassing metadata available. These are candidate inputs, not
an adopted SixDB ABI. An x86 SIMD implementation and native target-host runs
are still needed before choosing architecture-specific widths or kernels.

Validation passed 26,738 native full-mask/tail/boundary/text/replacement checks
on the final text-enabled source, both optimized and under ASan/UBSan. The
Python model passed 216 Boolean, 736 rollup, 1,449 bin-boundary, and 204 history
checks. It is a small semantic model, not a concurrency or recovery proof.
All timed comparisons passed their oracle and repetition-accounting checks.
