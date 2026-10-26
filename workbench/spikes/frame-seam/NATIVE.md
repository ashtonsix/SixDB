# Native span comparison

This is the historical spike qualification. The implementation has since landed;
[final landing qualification](landing/README.md) includes the mask kernels and
ISA density policy.

**Keep the span seam and its contribution laws.** It simplifies the caller and
improves the tested range shapes on Zen 5. Keep coarse/full/partial distinctions
through lowering: forcing one partial edge to change the interior's algorithm
was the principal failure of the first attempt. Dense fine selections retain a
small regression on one narrow column; this is visible below.

The qualified candidate is **38695dc4**, against original Frame **7fa3ad56**.
The final candidate includes the borrowed-mask fast path and vectorisable
reference defaults, plus the endpoint-grant guard. These results do not include
subsequent neutral-mask experiments or a SeriesPack leaf repair. Nothing here
promotes the candidate into the main checkout.

## Comparison boundary

AMD EPYC 9R45 (Zen 5), one pinned CPU on c8a.xlarge (4 vCPU, 8 GiB), Clang
21.1.8, `-O3 -gline-tables-only`, common function/block alignment flags. AVX-512
uses `-march=znver5`; AVX2 uses `-march=x86-64-v3`; both tune for Zen 5. Timed
binaries have logical counters disabled.

The original Frame headers and sources are exported byte-for-byte from the base
commit. Both implementations link **the identical SeriesPack/Intervals archives**
per profile. This isolates Frame changes; it does not measure future leaf fixes.
The source and binary hashes, input reference, flags and complete repetitions
are retained in [the final evidence](evidence/native-final/artifact.json).

`span_compare.cpp` runs three arms: original/current caller, candidate/current
caller, candidate/span caller. The current caller uses coarse SUM for wholly
selected blocks and masked SUM only for genuine partial blocks. Reusable caller
scratch is allocated outside timing; its per-command mask work is timed.
Oracles run outside timing and timed results consume both u128 halves. Five
process rounds rotate arm order. All image byte counts/hashes match per case.

The 24 equally weighted scenarios are six recipes (pack, bound, sizes16/64,
relative16/64), two authored distributions and two cardinalities (9001/65536).
The table is the geometric mean of each case's median time ratio; it is not a
workload-weighted database speedup. Smaller is better.

| SUM shape | AVX-512 time/base | AVX2 time/base |
| --- | ---: | ---: |
| Whole unit | 0.68 | 0.83 |
| Whole group | 0.74 | 0.89 |
| Across group edges | 0.62 | 0.77 |
| 63 rows | 0.57 | 0.57 |
| Tail | 0.59 | 0.66 |
| Coarse group holes | 0.74 | 0.88 |
| Span with coarse holes | 0.63 | 0.70 |
| Span with fine selection | 0.71 | 0.77 |
| Clustered fine selection | 0.76 | 0.80 |

The three-arm comparison also isolates the span caller on the **same candidate**:
whole-unit/group calls stay near parity (0.99/1.00 and 0.99/1.01); cross-group
edges are 0.86/0.93, tails 0.76/0.85, spans with coarse holes 0.80/0.87 and
spans with fine selection 0.91/0.89. Thus the larger table gains include stage-law
improvements; the span overload adds further savings where the caller formerly
built partial-block masks. Ratios in each pair are AVX-512/AVX2.

Every individual tested edge/short/tail/fine span improves. Some already cheap
whole/group bound cases lose: worst whole-group ratios are 1.12/1.10, and worst
coarse-hole ratios 1.12/1.13. Their fixed request cost remains a target for
prepared/reused requests; larger aggregate wins do not erase these cases.

## Existing operations and remaining gaps

The regression panel also uses real ClickBench slices and the existing generated
timestamp trace: 76 cases per arm, five rounds, repeated and 256 MiB eviction
conditioning. These are conditioning labels, not proof of L1 or DRAM residence.
Representative **repeated** ratios of candidate time to original:

| Operation | AVX-512 | AVX2 |
| --- | ---: | ---: |
| EventTime SUM | 0.39 | 0.75 |
| EventTime selected SUM, period 3 | 0.66 | 0.79 |
| EventTime clustered selected SUM | 0.34 | 0.47 |
| EventTime FILTER | 0.38 | 0.42 |
| EventTime COUNT | 1.01 | 1.01 |
| Generated timestamp COUNT | 0.30 | 0.26 |
| OS selected SUM, period 3 | 0.89 | **1.06** |
| UserID selected SUM, period 3 | 0.93 | 1.00 |
| URLHash selected SUM, period 3 | 0.89 | 1.00 |
| UserID bind | 0.071 | 0.072 |
| OS bind | 0.16 | 0.17 |

The bind win is the separately identified payload-light bound proof; the
checkpoint SUM law contributes to old-API SUM too. The three-arm raw data keeps
these implementation gains distinguishable from the additional span interface.

The clearest residual is **OS period-3 selected SUM on AVX2**: 8371 to 8883 ns
repeated (+6.1%), and 29620 to 32860 ns after eviction (+10.9%). Its earlier
freeze-2 regression was +20.9% repeated; the borrowed-mask change removed most
of it. This narrow-column case is a Frame mask-path cost: the AVX2 wide exact-SUM
fallback cannot explain it. Remaining targets are repeated mask classification,
counts and stage transitions. Packed masked totals can improve the absolute
leaf cost as a separate experiment.

Other regressions above 5% are retained rather than discarded: AVX2 UserID
alternating COUNT is +5.8% (310 to 328 ns); AVX-512 OS full SUM +5.2% (2548 to
2680 ns); three eviction-conditioned cases are +5.6–6.0%, including unchanged
point paths. AVX-512 OS period-3 SUM also loses 5.2% after eviction. The
complete cell report can be regenerated below.

## Masks as contributions

The source audit found several distinct issues. Frame's generic fine SUM/COUNT
walks selected bits after decode. SeriesPack already masks vector values in its
AVX-512 kernels. Its AVX2 exact-halves path, however, stores values to an array
and walks selected lanes scalarly for unweighted widths 57–64 (weighted widths
49–64); that separate leaf deficiency remains. ARM here has a NEON backend;
SVE2 has not been implemented or measured.

Ashton's principle is the right dense-kernel rule: neutralise inactive
contributions, then reduce. For SUM use zero; for COUNT intersect predicate bits;
for unsigned MIN/MAX use the identities. Bound can keep a masked residual SUM,
`lo * popcount(mask)` and exception corrections. Intervals must preserve its
input dependencies: unselected deltas can still affect a selected later value.

Claude's separate NEON diagnostic (d9a2ea72, branch `spike/frame-seam-mask`)
resolved the initially negative examples: the rejected SUM vectorised, but lost
to the bit walk at one-third density; the rejected summary failed to vectorise
until conditional updates became identity-element reductions. Measured crossings
were around 40% SUM / 50% summary selection there. This supports a block-level
dense/sparse choice; these thresholds are not qualified for Zen 5, and those
experimental kernels are not part of this candidate.

## Qualification and recovery

Both ISA profiles pass 11 affected CTests, **48,832 independent span checks**,
the masked-demand witness and 692,915 native-control checks. Every profile has
3240 unique span records (216 cases x three arms x five rounds) and 760 unique
regression records. The worker manifest matches all 467 candidate Ikea source
files and 25 original Frame files. Local ASan/UBSan passes nine fixtures, with
SeriesPack/Intervals/core archives explicitly uninstrumented.

Final job: `20261026T064647Z-2dd7b071`, capture digest
`7d68a49a513e324f77d24f04cbe3728f4257ad6ee7a6a70e31c427b4af874cdf`.
Its script took 311 seconds. The earlier c9810e8a diagnostic is retained as
[native-first](evidence/native-first/artifact.json). Both small workers are
verified terminated; no large-instance or saturation campaign was used.

The archive includes the captured `native.sh`, its baseline export/CMake and the
paired runner in `source.tar.gz`. Input columns are references to the shared
cache, not copied into results. To regenerate tables and every case offline:

```sh
python3 workbench/spikes/frame-seam/native_report.py \
  workbench/spikes/frame-seam/evidence/native-final build/reports/frame-seam
```

The ordinary usage is `reader.sum(row_range{first,count}, coarse, fine)`;
`required(span, coarse)` preserves masked group skipping during acquisition.
Group ownership, block masks and logical output positions stay distinct. A
prepared read handle could retain the admitted/lowered request for repeated
execution behind this same logical API; it must retain its interpretation and
borrowed-mask lifetimes. This is an architectural fit, not a timed prepared-handle
implementation. Edge processing may widen only inside granted backing; a mask
does not grant additional reads. Storage geometry remains
block256/group4096/part65536; alternative group sizes and prepared writers remain
research.
