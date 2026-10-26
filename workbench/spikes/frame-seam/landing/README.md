# Frame span and mask landing

The implementation is in Ikea, starting at **e6138670**; **8e79b48c** adjusts
only the default reductions' ISA density thresholds. Ordinary callers use
`sum(row_range, coarse, fine)`, `count(op, threshold, row_range, coarse)` and
`required(row_range, coarse)`. Whole-unit calls remain available.

The stage owning a transform applies its law before lowering into groups and
blocks. Full interior runs keep range kernels across partial edges. Selection,
child contributions and dependency closure remain distinct: repeated uses of a
checkpoint retain their multiplicity. Requests lend fine bitmaps, and narrowing
preserves truthful cardinality without mutable lazy state. Ordinary reader
headers do not expose the author implementation. Experimental counters are
excluded. See [usage](../../../../ikea/docs/frame/usage.md) and the
[source/seam map](../../../../ikea/docs/frame/reference.md#source-and-extension-map).

The maintained span/demand fixtures include guard pages, nonzero fine origins,
short borrowed masks, unavailable masked-out groups, partial tails, source order,
all comparison laws and independent original-value totals. The ordinary example
uses the span overload. Existing images retain the same byte law.

## Native qualification

Whole-table source: e6138670. Zen 5 (EPYC 9R45), AVX-512 and AVX2, each passes
18 affected CTests with `SIXDB_TEST_SCOPE=full`, including SeriesPack and filters.
Graviton 4 NEON passes 13 affected Frame/Intervals/selection checks. Each profile
also passes 692,915 native-control checks and the old/current direct leaf oracle
(including weighted+masked sums, periods 16/64, nonzero starts and both u128
halves). Local release and nine ASan/UBSan fixtures pass; linked leaf archives
are uninstrumented in the local sanitizer run.

Each profile contains five rotating process rounds: 4320 span records, 1140
existing-operation records, and 440 typed-leaf records. Image bytes/hashes match
between arms. Worker manifests match all 468 frozen Ikea files and 51 original/
qualified Frame control files. Common current SeriesPack/Intervals archives
isolate the Frame changes; a separate header-only old-expression/current-body
comparison isolates the AVX2 repair. All timed leaf calls have a compiler memory
barrier and consume both u128 halves. Controls are original Frame 7fa3ad56 and
the qualified span candidate 38695dc4.

- [Zen 5 evidence](evidence/zen5/artifact.json), job `20261026T075445Z-d1d02362`.
- [Graviton 4 evidence](evidence/graviton4/artifact.json), job `20261026T080929Z-c8a5894f`.
- The initial Graviton full-matrix build (`20261026T075457Z-79e5f003`) exhausted
  8 GiB while compiling the unchanged monolithic SeriesPack wire test. It ran
  no checks and is not qualification evidence ([recovery](evidence/initial-graviton-build.json)). The follow-up keeps the affected
  surface and direct leaf controls; the NEON leaf body is unchanged. Both x86
  profiles retain full-width qualification for the changed AVX2 leaf.

The whole-table timing cells below are measured on e6138670, before the final
threshold-only change. Final policy evidence is recorded separately below;
these are not claimed as a remeasurement of every whole-table cell.

## Whole-table results

Geometric means over 24 equally weighted recipe/data/cardinality scenarios;
ratios are landed-candidate time / original-Frame time. Lower is better. These
are operation-shape comparisons, not database-workload speedups.

| SUM shape | AVX-512 | AVX2 | Graviton 4 |
| --- | ---: | ---: | ---: |
| Whole unit | 0.68 | 0.83 | 0.72 |
| Whole group | 0.75 | 0.89 | 0.80 |
| Across group edges | 0.63 | 0.76 | 0.71 |
| 63 rows | 0.58 | 0.59 | 0.65 |
| Tail | 0.59 | 0.65 | 0.70 |
| Coarse holes | 0.75 | 0.89 | 0.80 |
| Span + coarse holes | 0.61 | 0.68 | 0.67 |
| Span + fine mask | 0.71 | 0.76 | 0.77 |
| Clustered fine mask | 0.76 | 0.80 | 0.81 |

Against the already-qualified span candidate, shape means are 0.94–1.00 on
AVX-512, 0.97–1.01 on AVX2, and 0.95–1.01 on Graviton. The earlier span gains
survive extraction and mask integration. Individual cells still lose: the full
raw cohorts include every result, including small bound/group cases.

The existing-operation panel includes real ClickBench slices and generated
timestamps, with repeated and 256 MiB eviction conditioning. Conditioning does
not prove a particular cache or DRAM residency. Remaining losses include AVX2
OS period-3 selected SUM after eviction (+8.9% vs original, +0.9% vs qualified),
AVX-512 UserID alternating-block SUM repeated (+6.4% vs qualified), and Graviton
OS materialized points repeated (+9.3% vs qualified). Some eviction-conditioned
point cells lose 10.6–11.5% vs qualified. The point implementation is unchanged;
these observations remain unresolved, not erased from the scorecard or counted
as preserved per-cell wins. This landing preserves aggregate span gains and
makes no universal point-access advantage claim.

## Exact-SUM leaf repair

On AVX2 the scalar per-bit exact-halves fallback now accumulates low/high
32-bit halves in four u64 lanes, applies selection as zero contributions, and
folds to u128 at the existing bounded interval. The native leaf comparison gives:

| Direct typed-reader operation | Current / old time |
| --- | ---: |
| Weighted SUM, widths 49/56/57/64, period 16 | 0.29–0.34 |
| Weighted SUM, same widths, period 64 | 0.30–0.38 |
| Width 57 plain exact SUM | 0.69 |
| Width 64 plain exact SUM | 0.26 |
| Width 64 masked exact SUM, all selected | 0.53 |
| Width 64 masked exact SUM, rotating one-third mask | 0.86 |
| Width 64 masked exact SUM, none selected | **1.15** |

The all-zero wide leaf case remains worse in isolation. Frame eliminates empty
blocks before invoking leaves. AVX-512 and NEON bodies are unchanged; their
control timing differences are not attributed to the AVX2 repair. Full disassembly
is recoverable from the captured sources in the evidence archive.

## Density policy

The production defaults choose a bit walk for sparse partial blocks and a dense
identity reduction for sufficiently selected blocks. Full blocks bypass the
choice. Exact SUM uses u64 halves; summary uses zero for SUM/MAX and UINT64_MAX
for MIN, with unconditional reductions. No new caller setting is exposed.

Native primitive measurements show different crossovers from the initial Mac
NEON diagnostic. At half selection, dense summary / bit-walk time is 0.40 on
Zen 5 AVX-512, 1.17 on Zen 5 AVX2 and 1.28 on Graviton. At three-quarters it is
0.24, 0.70 and 0.85. The final conservative defaults, in selected rows per 256,
are SUM/summary = 85/85 (AVX-512), 103/160 (AVX2), and 115/192 (NEON).

The policy follow-up compiles the actual production `stage.cpp` and a decoded-only
leaf, including request construction, against the qualified, first and final
policies. It checks independent SUM/summary oracles over extreme values, 13
selection densities, nonzero starts and a partial tail. It needs no SeriesPack
rebuild. This isolates the policy decision; it is not a whole-codec comparison.

The follow-up passes 780 checked records per ISA (52 cases × three policies ×
five rotating process rounds), using the exact final stage source from
8e79b48c. Source hashes match that commit. Representative final / qualified
span-policy times, with independently generated masks at each stated selection
probability, include request construction and decode:

| Selection probability | AVX-512 SUM / summary | AVX2 SUM / summary | Graviton SUM / summary |
| --- | ---: | ---: | ---: |
| About one-third | 0.88 / 0.88 | 1.04 / 1.01 | 0.99 / 0.98 |
| Half | 0.59 / 0.54 | 0.82 / 1.01 | 0.96 / 0.97 |
| Three-quarters | 0.38 / 0.38 | 0.54 / 0.83 | 0.67 / 0.97 |
| 255/256 | 0.48 / 0.37 | 0.58 / 0.73 | 0.61 / 0.83 |

The AVX2 one-third sparse SUM case is 3.7% slower; the full cohorts retain it.
Compared with the initial policy, raising Graviton's summary crossover removes
about 19% time at five-eighths density; lowering AVX-512's summary crossover
removes about 39% at half density. Full-block and sparse shortcuts remain.

- [Final policy, Zen 5](evidence/policy-zen5/artifact.json),
  `20261026T083200Z-eb6a71e9` (13.5 seconds of script execution).
- [Final policy, Graviton](evidence/policy-graviton4/artifact.json),
  `20261026T082634Z-83e2e6b6` (9.5 seconds).

An initial x86 policy launch used an invalid Clang tune spelling and exited
before compilation; the corrected script uses `znver5`. It provides no evidence.
All worker instances are terminated. Full logs, sources and binaries that were
collected are recoverable through the artifact references; inputs remain shared
references rather than copied datasets.

To regenerate every whole-table cell from either retained CPU cohort:

```sh
python3 workbench/spikes/frame-seam/native_report.py \
  workbench/spikes/frame-seam/landing/evidence/zen5 build/reports/frame-landing-zen5
python3 workbench/spikes/frame-seam/native_report.py \
  workbench/spikes/frame-seam/landing/evidence/graviton4 build/reports/frame-landing-arm
```

## Remaining research

Private writers/preparation, prepared execution handles, alternative group sizes,
ordered runs, selected sizes contributions and extra encoding stages remain in
research. Acquisition currently accepts coarse masks only; a fine-empty group
may be acquired conservatively even though execution skips it. Select and point
paths are not converted into new execution modes by this landing.
