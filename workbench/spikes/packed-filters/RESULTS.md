# Native filter results — 15 October 2026

The filter API is implemented and the requested native targets have been measured.
Direct Local bitplane comparison is especially useful on NEON. GFNI reconstruction
usually wins independent throughput on the newer x86 targets, so it remains the
selected path above one bit there. These measurements do **not** establish universal
parity with the independent fused controls: the remaining losses are recorded below.

## Reading the numbers

All numbers are median **ns per complete window**, with five repetitions. Lower
is better. Dependent latency includes a result-to-next-address recurrence; independent
throughput uses eight separate recurrences. Both include query loading, address
mixing and loop control. A 16-value window is the default; the last table uses 64.
The warmed working set is at most 8 KiB or 32 KiB of payload respectively, plus
queries. No DRAM, Loom-ring, cache-residency qualification or hashtable speedup is
claimed. [Method and exact cases](README.md#measurement) include the benchmark
limitations, independent control definitions and rejected early diagnostics.

AVX2 means x86-64-v3 without GFNI; the retained x86 runs use Zen 5 tuning and
`znver5` for AVX-512, including GFNI. This includes the Granite decision and
selected cohorts: their worker-group environment override was misplaced and
the script used its Zen 5 default. They are native Granite measurements of the
same compiler profile used on Zen 5, not Granite-tuned results. Ikea and the
independent control use matching flags. Retained x86 LLVM-MCA models also target
Zen 5. The archived job environment and compiler
setup receipt establish those settings. The runner configuration is now corrected;
fresh Granite runs therefore use a different compiler profile. The later
[composition study](../filter-composition/RUNTIME.md) separately contains a
verified Granite-tuned cohort.

NEON is armv8-a+simd tuned for Neoverse V2. The compiler is pinned
Clang 21.1.8; automatic loop and SLP vectorization are disabled. Assertions remain
enabled, including control-versus-oracle checks before measurement.

## Sixteen-value equality

This selection includes narrow packed wins, ordinary byte parity and the widest
AVX2 loss. All five predicates and every timed width remain in the paired CSVs.

| Host / ISA | SeriesPack width | Ikea latency | Fused latency | Ikea throughput | Fused throughput |
|---|---:|---:|---:|---:|---:|
| zen5-avx2 | 3 | 3.92 | 6.89 | 1.56 | 1.08 |
| zen5-avx2 | 8 | 4.37 | 4.50 | 0.58 | 0.57 |
| zen5-avx2 | 64 | 8.00 | 6.46 | 1.06 | 0.85 |
| zen5-avx512 | 3 | 6.25 | 6.80 | 0.84 | 1.04 |
| zen5-avx512 | 8 | 4.84 | 4.83 | 0.60 | 0.61 |
| zen5-avx512 | 64 | 5.96 | 5.70 | 1.12 | 1.14 |
| granite-avx2 | 3 | 5.83 | 7.94 | 1.90 | 2.40 |
| granite-avx2 | 8 | 4.39 | 4.39 | 0.78 | 0.71 |
| granite-avx2 | 64 | 9.46 | 6.97 | 1.74 | 1.27 |
| granite-avx512 | 3 | 7.31 | 8.43 | 1.12 | 1.33 |
| granite-avx512 | 8 | 5.43 | 5.43 | 0.77 | 0.76 |
| granite-avx512 | 64 | 6.69 | 6.45 | 1.47 | 1.60 |
| neon-v2 | 3 | 6.41 | 16.24 | 2.03 | 3.64 |
| neon-v2 | 8 | 10.12 | 10.50 | 1.47 | 1.47 |
| neon-v2 | 64 | 13.07 | 13.90 | 5.76 | 6.52 |

## Ordered and TuplePack comparisons

TuplePack is a bound runtime one-code projection of five bits at bit offset three,
across sixteen tight byte rows. Its fused control knows the field constants at
compile time, extracts them in registers and compares; it does not call Ikea.
The table deliberately retains that stronger specialization advantage.

| Host / ISA | Case | Ikea latency | Fused latency | Ikea throughput | Fused throughput |
|---|---|---:|---:|---:|---:|
| zen5-avx2 | Series 3-bit < | 4.96 | 7.34 | 2.28 | 1.15 |
| zen5-avx2 | Series 7-bit < | 9.40 | 9.87 | 3.72 | 2.32 |
| zen5-avx2 | Tuple 5-bit == | 4.61 | 4.95 | 0.78 | 0.63 |
| zen5-avx512 | Series 3-bit < | 6.24 | 6.82 | 0.84 | 1.04 |
| zen5-avx512 | Series 7-bit < | 5.82 | 7.12 | 0.93 | 1.37 |
| zen5-avx512 | Tuple 5-bit == | 5.03 | 5.12 | 0.79 | 0.68 |
| granite-avx2 | Series 3-bit < | 7.03 | 8.10 | 2.60 | 2.51 |
| granite-avx2 | Series 7-bit < | 11.28 | 11.72 | 4.59 | 5.16 |
| granite-avx2 | Tuple 5-bit == | 4.57 | 4.39 | 1.23 | 0.77 |
| granite-avx512 | Series 3-bit < | 7.33 | 8.58 | 1.12 | 1.34 |
| granite-avx512 | Series 7-bit < | 7.69 | 8.79 | 1.21 | 1.77 |
| granite-avx512 | Tuple 5-bit == | 5.29 | 5.17 | 1.10 | 0.78 |
| neon-v2 | Series 3-bit < | 7.74 | 16.24 | 2.61 | 3.64 |
| neon-v2 | Series 7-bit < | 23.14 | 19.94 | 5.43 | 7.43 |
| neon-v2 | Tuple 5-bit == | 10.40 | 10.42 | 1.97 | 1.55 |

## Sixty-four-value equality

These are complete storage-facing calls, including grouping and mask assembly.
The earlier decision cohort measured native-group entries directly; compare
candidates within each cohort, not its older `series64` Ikea timings to these.

| Host / ISA | Width | Ikea latency | Fused latency | Ikea throughput | Fused throughput |
|---|---:|---:|---:|---:|---:|
| zen5-avx512 | 8 | 5.48 | 4.88 | 0.69 | 0.61 |
| zen5-avx512 | 64 | 7.00 | 6.92 | 2.23 | 1.58 |
| granite-avx512 | 8 | 5.43 | 4.64 | 0.79 | 0.73 |
| granite-avx512 | 64 | 7.88 | 8.09 | 3.17 | 2.65 |
| neon-v2 | 8 | 13.50 | 12.81 | 5.32 | 3.93 |
| neon-v2 | 64 | 30.82 | 35.28 | 23.01 | 30.31 |

## What the packed experiment changed

Local residuals already store eight original-position bits in each plane. Equality
intersects each plane or its complement according to the query bit. Ordered
comparisons propagate a less/greater mask through the planes without reconstructing
individual numbers. Two tiles form the sixteen-position answer. This saves the
transpose and the final SIMD-to-scalar mask conversion, but consumes scalar
instructions and loads proportional to the number of planes.

The selected policy uses direct planes for Local equality at widths 1–7 and
ordered comparisons at widths 1–5 without GFNI. Larger ordered predicates retain
decoded SIMD lanes: on V2, for example, the decision cohort's 7-bit `<` direct
path improved latency (11.64 vs 23.88 ns) but reduced throughput (6.36 vs 5.41 ns).
With GFNI only width one selects direct planes. The decision cohort's Granite
3-bit equality direct path had 6.50 ns latency but 1.89 ns throughput; GFNI decoding
gave 7.59 ns and 1.21 ns. This is a throughput-conscious policy, not a claim that
one path minimizes both objectives. The final Zen AVX2 rows above expose another
trade-off against the stronger independent decoder, even after improving the
existing Ikea decoder path.

TuplePack fully active, tight byte columns mask neighboring fields and compare
encoded bytes to the shifted query. The shift preserves unsigned order. Single
rows use a bounded scalar byte access; sparse or strided projections keep their
existing prepared-reader access semantics. Already decoded native packets can
also be filtered without reloading, so a caller can share decoding across several
predicates instead of repeating it.

## Remaining performance limits

- **AVX2 64-bit mask assembly.** The measured Zen binary has extra narrowing,
  blending and packing between comparisons and the final mask in Ikea's inlined
  consumer. Its independently authored control emits a shorter packing sequence.
  The loops are fully fused; this is an instruction-selection/reduction issue,
  not callback overhead or an extra memory indirection. The absolute loss is
  visible in both x86 AVX2 rows. A better explicit multi-register reduction is
  still useful work; this delivery does not claim it is solved.
- **Large windows.** Direct ZMM loads removed unnecessary YMM assembly for dense
  standard-width AVX-512 groups, but mask assembly/scheduling still leaves a
  throughput gap at 64-bit width. NEON's storage-facing 64-byte window also trails
  the independent control, although its 64-bit window wins. Native-group timings
  alone would have hidden the former loss.
- **Runtime TuplePack descriptions.** Binding is outside the timing; field
  metadata still reaches the inlined consumer. Versus a fixed field control,
  5-bit equality throughput costs about 0.11–0.46 ns per sixteen rows across the
  five profiles. No extra public compile-time layout API was added to conceal it.
- **Coverage is deliberately bounded.** All SeriesPack widths 1–64 are checked,
  but timing covers Local 1–8/16/32/64, full activity and the listed TuplePack
  columns. Sparse/strided filters, other widths, larger-than-cache scans,
  selectivity distributions, and fused multi-predicate consumers are not
  performance-qualified by these results. There is no SVE2 result.

These filters are suitable building blocks for fingerprint and suffix selection:
they retain original coordinates and can intersect an existing occupancy mask.
The measured byte equality paths are close to independent fused implementations.
That establishes primitive utility, not an end-to-end hashtable result; hashtable
was not modified or reopened.

## Correctness, code generation and evidence

The final native checks passed in all five profiles. Local Release and
ASan/UBSan passed with the same selected filters. Existing SeriesPack, TuplePack
packet and TuplePack GPR regression checks also passed. Checks use independent scalar
predicate results, every SeriesPack width, Local/eligible Striped layouts,
separated heads, arbitrary windows and sparse activity, unsigned boundaries,
substituted children, TuplePack original-row versus packet-byte coordinates,
invalid admission and guard pages. Benchmark fixtures independently encode their
Local input and verify both timed implementations before running.

The actual measurement-function audits found zero unexpected outlined calls:
165 functions in each AVX2 binary and 205 in each AVX-512/NEON binary. Inspection
also follows the complete result through multiplication/high-bit extraction into
the next address. The audit itself establishes fusion only, not that dataflow.
Early cohorts with outlined lambdas or a recurrence that discarded upper mask
bits were rejected, and their timings are not used here.

Bounded assembly witnesses and LLVM-MCA models were produced on every native
target. For example, the NEON byte-equality witnesses for Ikea and the independent
control both model 3.5 cycles reciprocal throughput (17 static instructions).
This is an isolated scheduling model including the witness's branches/return,
not the observed ns/window and not a substitute for examining the complete loop.
Full disassembly and MCA output remain in each recoverable archive; the compact
assembly witnesses and fusion audits are retained below.

| Cohort | Zen 5 | Granite Rapids | Neoverse V2 |
|---|---|---|---|
| Selected implementation | [paired samples](evidence/selected-zen5/) | [paired samples](evidence/selected-granite/) | [paired samples](evidence/selected-neon/) |
| Decision: decode vs packed | [alternatives](evidence/decision-zen5/) | [alternatives](evidence/decision-granite/) | [alternatives](evidence/decision-neon/) |

The selected source capture hash is
`ae6c9a99e327a4afaa347594750fd1ea585c821bbaa7b575aaf55e0acc2a1a62`.
Each directory's `artifact.json` recovers its full source, manifest, measurements
and generated code. The decision cohort precedes automatic packed dispatch,
the final dense ZMM loading change and the storage-facing 64-value benchmark.
Its retained alternatives explain the selection, rather than serving as another
current implementation scorecard. Two unrelated pre-existing working changes
(the NEON single-load read and an Orbital include removal) were in the captures
and were left untouched by this task.

To regenerate medians and extrema from all retained selected measurements:

```sh
orb -m ubuntu python3 workbench/spikes/packed-filters/report.py \
  zen5-avx2=workbench/spikes/packed-filters/evidence/selected-zen5/avx2.csv \
  zen5-avx512=workbench/spikes/packed-filters/evidence/selected-zen5/avx512.csv \
  granite-avx2=workbench/spikes/packed-filters/evidence/selected-granite/avx2.csv \
  granite-avx512=workbench/spikes/packed-filters/evidence/selected-granite/avx512.csv \
  neon-v2=workbench/spikes/packed-filters/evidence/selected-neon/neon.csv \
  --output build/packed-filters/report.csv
```

## Build and composition cost

The original single-file correctness matrix was killed on the 2 GiB NEON worker.
Its job receipt recorded about 1.44 GiB maximum process RSS; that is evidence of
build pressure, not a kernel-level attribution of the kill. The matrix expands
182 physical width/layout/head cases across five predicates. Splitting the test
by width and the benchmark by predicate allowed these workers to complete; this
bounds each translation unit's peak rather than eliminating total expansion.

Tooling's [bounded composition control](../filter-composition/README.md)
found that simply sharing cold test traversal behind typed outlined assertions
was not a good fix: linked text grew and peak memory barely changed. A more
useful control normalized decoded native carriers by byte width, reused the
existing `native_chain` for shared decode and compare stages, and retained
selected fused paths.

Across all 64 widths × five predicates, executable `.text` fell from 179,604 bytes
(all fused) to 65,284 bytes (selective fusion); all-shared was 58,468 bytes. Peak
compiler RSS fell from about 534 to 317 MiB. All 47 retained fused object sections
were byte-identical, and 30,720 independent-oracle windows per profile passed.
This was one serial, instrumented local build per variant, not native hardware
performance or complete production build cost. The earlier representative control
and rejected cold-harness split remain in that report's recoverable archive.

The [native runtime follow-up](../filter-composition/RUNTIME.md) measures the cost
of those shared boundaries on AVX2, AVX-512 and NEON. Its selected pattern keeps
ordinary hot filters inline and permits canonical shared consumers within an
existing composed operation. [Ikea's guide and maintained examples](../../../ikea/docs/filters.md)
carry that pattern into the module without a mandatory CPS entry or dispatch policy.
