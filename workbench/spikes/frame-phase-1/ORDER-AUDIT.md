# Read timing order audit

The large run-search and logical-head gains survive calibration. Close rankings
from the earlier cyclic-order cohorts should not select a program. Use the
qualified comparisons below; retain those older cohorts as history.

Claude found that cyclic arm rotation preserved each arm's predecessor in its
encoding driver: identical-work controls differed by 0.62–1.52×. The point and
filter drivers also rotated cyclically. Reversing a second process did not
balance the other predecessors. This was a measurement issue, not a codec change.

## Corrected conditioning and controls

Both drivers now copy two exact `std::function` targets and contexts, changing
only the labels. Request generation retains seed713 and original coordinates.
Order has a separate RNG. Each complete Williams-square round balances every
arm's position and every within-trial predecessor pair; labels and trial order
are shuffled with seeds1729/17291 across the two processes. Odd populations use
the reversed square too. Requested trials round up to complete rounds. The
independent check counts all positions/pairs for1..32 arms:11,440 checks pass.
Each point arm warms its actual requested batch; each filter arm now warms its
own complete query before timing. This is a warm-work comparison, not proof of
whole-image cache residency or cold throughput.

The first random-shuffle diagnostic still had alternating-mask duplicate
outliers up to36%. Balanced order improved that; a native filter duplicate
still differed31% on one UserID alternating-mask scan without same-arm
conditioning. The final filter prewarm removes that discrepancy. All three
cohorts remain in recovery; none were pooled.

Final identical-work ratios (duplicate/original median time):

| Surface | Cells | Minimum | Median | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Points, all-selected non-tiny cells, both domains | 30 | .960 | .999 | 1.043 |
| Filters, queries taking at least5µs | 125 | .960 | 1.001 | 1.046 |
| Filters, all-selected queries taking at least5µs | 64 | .964 | 1.001 | 1.026 |

The complete calibration retains none masks, partial masks, tiny inputs and
metadata-only shortcuts. Some O(1) filter samples quantise to zero; their ratios
and per-value rates cannot rank programs. These observed ranges are calibration,
not confidence intervals or a universal error bound. Do not attribute the
remaining movements to a branch/cache mechanism without further evidence.

## What survives

Independent complete-image points, all blocks selected, native16 checkpoints,
median ns/request. The two compact programs share body bytes and native residual
prefixes; only the run-source type changes:

| Column | Projection | Maintained native run source | Time reduction |
| --- | ---: | ---: | ---: |
| OS | 22.079 | 14.609 | 34% |
| ResolutionWidth | 23.087 | 15.134 | 34% |
| UserID | 31.046 | 20.259 | 35% |
| RegionID | 28.104 | 18.799 | 33% |
| ClientIP | 22.010 | 13.590 | 38% |

That supports the curated unpredictable run program. Small non-RLE differences,
including WatchID7.264→7.101ns, do not select a policy. Native FOR arrays remain
much faster for points; this audit does not close that gap.

Complete median-threshold COUNT, all blocks selected, ns/original value:

| Column | Encoded owner | Logical H16 | Block bounds | Window q8 | Native FOR |
| --- | ---: | ---: | ---: | ---: | ---: |
| Jena pressure | .734 | .104 | .308 | .330 | .147 |
| Jena temperature | .502 | .098 | .210 | .254 | .147 |
| OS | .144 | .067 | .145 | 2.222 | .160 |
| ResolutionWidth | .164 | .271 | .162 | 2.178 | .155 |
| RegionID | .193 | 1.461 | .194 | 2.292 | .151 |

Logical heads still earn their bytes for some predicted/low-cardinality columns,
and still lose badly on RegionID/ResolutionWidth. Keep exact block bounds as the
small baseline; reject the current q8 implementation. Memory and preparation are
unchanged from the earlier filter cohort. Polars was not rerun here: compare the
native controls within this cohort, not old Polars times against new timings.

## Qualification and recovery

Clang21.1.8, O3/native ISA/generic tuning, local OrbStack AArch64 VM/vCPU7,
12 original inputs. Points have72 samples/cell; filters72 or76 depending on
arm population. Every consumed point sum and filter count agrees with original
values. The owner is frozen f760 material, separate from modern borrowed-block,
dictionary-cache and masked-operation experiments.

The [selected cell summaries and calibration](evidence/order-audit-local/)
retain all candidate/control cells, not just these examples. Recovery includes
raw diagnostics, binaries, exact replacement sources, compile commands, inputs
by hash and the runners. Point source capture is
`2d9f399ecb8ef9ff441263a3014ccdba40e5436d5d39025fa2e0a12f172cfa59`;
filter capture `9daf96bcf2a2e09e0937084d2d418f6c9f570040de3dd659a5ce76eac24e0cc3`.
