# GFNI and AVX2 Local indexof

SeriesPack `indexof` searches up to 64 unsigned packed values and returns the
first active matching position, relative to the window, or the window count.
A Local tile stores eight rows: byte-sized bodies followed by residual bitplanes.
The implementation compares the bodies together and compares all eight residuals
with one prepared query. For x86 default search it builds the whole window's
mask, avoiding data-dependent tile-hit branches.

The selected x86 paths preserve the public interface, stored representation and
exact read bounds. They reuse the existing arithmetic-only
`composition::local_equal8<R>` seam. No allocator, padding requirement, search
mode or caller-visible tuning choice was added.

## What is selected

- **GFNI:** prepare the reversed query once, transpose the supplied plane word,
  and produce an exact eight-bit mask. Public `indexof` uses this for complete
  Local tiles with R=4..7 and bodies of at least 32 bits, when GFNI and
  AVX-512BW/VL are enabled. Smaller-body and pure-plane defaults remain unchanged.
- **AVX2:** Zen 5 and Granite tuning use fast BMI2 PDEP to expand R=4..7 query
  bits into byte predicates once. A bit-parallel scalar reduction compares all
  eight residuals; body comparisons use explicit SIMD. Public `indexof` uses
  this to form a branchless whole-window mask for eligible Local mixed-width
  windows. Other BMI2 CPUs do not inherit a
  fast-PDEP assumption, and generic AVX2 keeps its existing lowering.
- **Owner composition:** a caller can pass an eight-byte word to the same
  prepared helper after proving it readable. Ordinary SeriesPack loads exactly
  R residual bytes, using overlapping smaller loads where needed. The helper
  does not own that proof or perform a load.

Only complete eight-row windows take this specialization. Heads, custom semantic
children, other storage geometries, scalar edges and other widths retain the
existing filter lowering. Query-domain rejection, activity, relative ranks and
count sentinels retain their original meaning. The non-AVX512 GFNI helper uses
vector compare plus movemask; its correctness is tested, but it is not an
additional timed/default `indexof` policy.

## Complete-consumer results

Final paired tables are generated from the qualification capture below. Each
query loads its runtime query, prepares it, compares bodies and residuals,
intersects activity, selects the first rank, and uses that rank to choose the
next window. No prepared query is supplied by the harness.

Varied hot queries, including runtime preparation and rank-dependent cursor work.
All entries are **ns/query**; lower is better. The independent fused column is
the branchless prepared full-mask control (arm 8).

### Dependent latency

| Host / ISA / K / N | Previous public | Ikea public | Independent fused |
| --- | ---: | ---: | ---: |
| zen5 / avx2 / 30 / 16 | 7.75 | 7.67 | 7.46 |
| granite / avx2 / 30 / 16 | 10.24 | 10.21 | 9.51 |
| zen5 / avx512 / 38 / 16 | 11.47 | 9.73 | 9.23 |
| granite / avx512 / 38 / 16 | 13.33 | 11.70 | 11.53 |

### Reciprocal throughput

| Host / ISA / K / N | Previous public | Ikea public | Independent fused |
| --- | ---: | ---: | ---: |
| zen5 / avx2 / 30 / 16 | 3.79 | 2.95 | 2.82 |
| granite / avx2 / 30 / 16 | 4.86 | 4.00 | 3.72 |
| zen5 / avx512 / 38 / 16 | 2.63 | 2.11 | 2.21 |
| granite / avx512 / 38 / 16 | 5.00 | 2.86 | 3.09 |

The shared public path is close to the independently fused full consumer,
not claimed to be free: these rows differ by up to 0.69 ns latency and 0.28 ns
reciprocal cost. The selected GFNI public paths can also slightly beat the
independent control in reciprocal cost; different code generation still matters.

The complete matrix retains latency losses: about 1.1 ns at Granite AVX2
K13/N32 and 1.0 ns at Zen 5 GFNI K62/N8. Every changed shape improves reciprocal
throughput in this captured matrix (all patterns and both measured activity
masks); that is a scoped observation, not a guarantee for all inputs/residences.

Full tables and all-cell medians: [Zen AVX2](evidence/x86-index/final/zen5-avx2/table.md),
[Granite AVX2](evidence/x86-index/final/granite-avx2/table.md),
[Zen GFNI](evidence/x86-index/final/zen5-avx512/table.md),
[Granite GFNI](evidence/x86-index/final/granite-avx512/table.md).

## Why these choices

**The short trace overstated the value of early exits.** Iceberg–Swiss's complete
ARM table lost with public `indexof`; the peer attributed it to unpredictable
first-tile branches. An additional hot x86 trace therefore queries every row,
uses a long salted recurrence, and keeps both query words and payloads resident.
It confirms a large reciprocal-throughput penalty even when dependent latency
looks good. The provisional x86 early-exit default is rejected:

| K30/N16, varied queries | Early public, latency / reciprocal ns | Independent prepared full, latency / reciprocal ns | Full filter + first bit, latency / reciprocal ns |
| --- | ---: | ---: | ---: |
| Zen 5 AVX2 | 7.15 /6.13 | 7.48 /2.82 | 7.47 /3.70 |
| Granite AVX2 | 9.51 /7.44 | 9.52 /3.70 | 9.96 /4.93 |
| Zen 5 GFNI | 8.91 /6.22 | 9.57 /2.19 | 9.95 /1.70 |
| Granite GFNI | 10.87 /7.15 | 11.31 /3.32 | 12.76 /3.55 |

The selected policy uses full prepared masks for tuned AVX2 and sufficiently
wide GFNI bodies. K30 GFNI retains its existing public lowering: the branchless
alternative saves little latency on Zen 5 while losing reciprocal throughput.
Owner-wide helper composition remains separately useful and requires its own
readability proof. No new early-exit selector is added to the public interface.

The candidate sweep measures nine consumers, including existing `indexof`,
early sixteen-row filters, an independent byte-column kernel, scalar residual
extraction at individual candidates, prepared bounded and owner-wide residual
kernels, and independently authored prepared body-first/full-mask kernels.

Scalar extraction is rejected. In the Zen 5 AVX2 K14/N32 shared-prefix panel it
costs 24.21 ns latency / 22.24 ns reciprocal, versus 8.87 / 5.00 for the original
public consumer. The independent prepared whole-residual path costs 7.35 / 4.58.
The problem is repeated candidate extraction, not a fundamental penalty for
keeping body and residual arithmetic composable.

GFNI is not imposed on pure bitplanes: Zen 5 K6/N8 bounded GFNI improves
reciprocal cost from 2.11 to 1.32 ns but increases latency from 5.22 to 7.32 ns.
The existing pure-plane default stays. For byte bodies, the existing wider
AVX-512 comparison can sustain better throughput than guarded eight-row groups.
For K14/N32 shared-prefix inputs on Zen 5, bounded GFNI body-first costs 3.26 ns
reciprocal versus 2.30 for the current full-mask consumer. These results rule out universal dispatch to the new arithmetic. The varied
query panel also rejects early tile-hit guards for wider bodies.

The stronger load proof matters. In the candidate K30/N16 random-hit panel,
Zen 5 GFNI bounded body-first costs 8.15 / 1.71 ns; owner-wide costs 7.47 / 1.52.
Granite costs 10.01 / 2.28 bounded and 8.53 / 2.04 owner-wide. These are complete
consumers with preparation charged, not prepared-core timings. The production
public path must retain the bounded loads.

## Owner-wide GFNI through the complete consumer

The final varied-query owner panel removes early tile-hit guards and compares
Ikea's helper with an independently fused owner-wide kernel. K30/N16 results
include runtime preparation, body comparisons, first rank and cursor work:

| Host | Previous public, latency / reciprocal ns | Composed owner helper, latency / reciprocal ns | Independent owner fused, latency / reciprocal ns |
| --- | ---: | ---: | ---: |
| zen5 | 10.20 /1.79 | 9.30 /1.58 | 9.01 /1.77 |
| granite | 13.10 /3.56 | 9.95 /2.42 | 10.28 /2.83 |

This stronger load proof makes GFNI useful for K30 without changing its ordinary
public default. The helper/independent kernels have different instruction
scheduling and mask fusion; neither is a universal latency/throughput winner.
[All owner panels](evidence/x86-index/owner/) retain both targets and both ISAs.
These are immutable, initialized owner windows. A real owner must also include
all loaded bytes in its synchronization/protection proof.

## Measurement and controls

The candidate sweep covers 19 K/N geometries; qualification adds K12/N64 and
K30/N64. Inputs include clustered and random bodies, early/late/uniform hits,
misses, mixed hits/misses, duplicates and shared-prefix miss/late-hit cases.
Every shape uses full and sparse activity. For small residual domains, the
shared-prefix fixture can span several bodies rather than making every body
identical. All input locations are offset by one byte. The timed working sets
are hot (64 windows, at most 16 KiB of payload-window address space in the
qualification), not LLC or DRAM claims.

**Latency** uses one rank-dependent stream. **Reciprocal throughput** uses eight
independently salted streams and reports ns/query, so smaller is better. Every
lookup charges query preparation. The full-active hit panels retain an actual
load/comparison/rank/address dependency. Predictable all-miss returns can bypass
that dependency; their serial column is a one-stream cost, not measured lookup
latency. Sparse/mixed cases can contain such misses. The finite 64-window trace
is not a proof against branch-predictor learning on arbitrary larger workloads.
The added `x86-entropy.cpp` panel chooses any row in the same hot payload windows
and uses a long salt recurrence. Its plain query-word arrays add at most 16 KiB.
Cursor mixing adds arithmetic, so compare implementations within each panel;
do not subtract a short-trace time from a varied-trace time as a kernel uplift.

Each panel has five repetitions in both consumer orders; retained summaries
include median, minimum, maximum and all ten observations' count. Clang 21.1.8
compiles explicit SIMD with both loop and SLP vectorizers disabled. AVX2 uses
`x86-64-v3`; AVX-512 uses the native `znver5`/`graniterapids` ISA. Tuning is
separate and explicit. Workers are pinned to one CPU; the two hosts run
independently, without colocating comparisons on another task's busy worker.

The independent prepared controls decode packed bodies with their own exact
loads, shuffles, compares and mask reductions. They do not call Ikea's
read/compare/mask helpers. They implement both guarded first-match and full-mask
consumers; neither is presumed universally best. The final AVX-512 control uses
native comparison masks. The older independent byte-column control remains for
word-width comparisons and to expose architecture sensitivity; its odd-width
results are not claimed as a lower bound.

Final qualification's **previous public indexof** control (arm 10) freezes the
original public lowering: the ordinary full filter followed by
`min(countr_zero(mask),count)`. Preliminary captures also have a **full filter +
first bit** control (arm 9) which inserts a count sentinel before `countr_zero`.
That has the same result but is not byte-identical to the old public wrapper.
The candidate sweep's arm 0 captures the actual pre-change public wrapper.
Arm IDs and source captures keep these controls distinguishable.

## Code generation and correctness

The owner-wide GFNI witness folds the eight-byte load into
`vgf2p8affineqb`; the bounded six-byte witness uses two overlapping four-byte
loads and a combine. Query preparation is a broadcast and affine transform.
The compiler expresses the R6 masked-zero comparison as an unsigned byte
comparison against four. Rank selection additionally needs the mask and its
first set bit. AVX2 uses PDEP plus expansion for preparation and shifted ANDs
for residual reduction; there is no loop over matching candidate rows.

[Isolated arithmetic MCA inputs](evidence/x86-index/codegen/core.s) include query
preparation and mask extraction, but exclude body comparison, domain validation,
rank/address consumption and cache effects. LLVM-MCA models bounded/owner forms
at 2.0/1.5 cycles reciprocal on Zen 5 and 3.0/2.0 on Granite. These are scheduling
diagnostics, not measured consumer latency or throughput.

Native `ikea_filters_check` and the ordinary API example passed on Zen 5 and
Granite in both AVX2 and AVX-512 profiles. Focused ASan/UBSan checks passed on all
four profiles, covering windows/activity/domain, Local/Striped/heads, exact page
ends and all prepared widths. GFNI without AVX-512 passed the exhaustive-query
prepared-mask tests on both hosts. Standalone public-header compilation passed.

A final native check also exercises runtime zero activity and out-of-domain
queries at an inaccessible page for K30/K38; volatile inputs prevent the caller
from erasing the guard test. All four native profiles passed. Existing tests
cover all K=1..64 and eight-row counts through 64, arbitrary origins, checked
bounds and authored substitutions. The prepared helper is compared against
independent per-row decoding for every in-domain query at R=1..7 and 1024 random
words per width, including arbitrary ignored upper bytes.

The [qualification receipt](evidence/x86-index/qualification.json) checks 77
current relevant source files against captured manifests. All match the last
native run. The larger qualification capture differs only by the subsequent
runtime-guard assertions; the final full native check includes those assertions.
Raw timings, binaries, complete disassemblies and captured sources remain in
the recorded S3 worker archives.

The [actual timed-loop audit](evidence/x86-index/codegen/timed-audit.json) follows
control flow between clock calls (the compiler lays the stop block before the
start block). It excludes warmup, truth checks and printing. The selected
AVX2 functions contain nine PDEP preparations: one in the serial body and eight
in the independent body. The GFNI helper consumers contain 27 affine operations:
one query plus two tiles for each of those nine lookups. There are no outlined
helper calls within the timed control flow. Source and native disassembly retain
branchless tile evaluation, exact bounded loads, and owner-only folded loads.


## Evidence and reproduction

- [Candidate summaries](evidence/x86-index/candidates/) and the
  [branch rejection panel](evidence/x86-index/branch-rejection/) retain all patterns and
  activity modes, including losses. Raw samples remain in the worker archives.
- `x86-index.cpp` owns the independent controls; `x86-index-report.py` aggregates
  samples; `x86-index-codegen.cpp` exposes standalone complete consumers.
- `x86-index-native.sh` runs the candidate sweep; `x86-graduate.sh` selects the
  actual-public qualification panel and runs production checks and sanitizers.
  `x86-entropy.sh` runs the longer hot trace, owner controls and native guard checks.
  `x86-index-workers.json` supplies the two native worker configurations.
- [Consumer documentation](../../../ikea/docs/filters.md) owns the public
  contracts; [the bit trick derivation](CONTAINS.md) owns the GFNI algebra.

Iceberg–Swiss consumes the same helper with separately proven initialized owner
windows at offsets 24 and 54 in its B64/K30 bucket. It also exercises public
`indexof`. These microconsumer results do not establish an end-to-end hashtable
speedup. Its [complete ARM f0/f11 comparison](evidence/arm-index/iceberg-indexof.json)
favors full filtering; the owning [ARM report](ARM-INDEXOF.md#icebergswiss-consumer-assessment)
records those losses and the peer's inlined-branch diagnosis. The peer owns the
separate x86 table comparison against its existing full-mask path.
