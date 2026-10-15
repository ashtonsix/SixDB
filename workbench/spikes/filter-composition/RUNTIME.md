# Runtime comparison of shared filter stages

This extends the [code-size control](README.md) with complete native consumers.
The existing production filter bodies and native-chain ABI are unchanged.
The selected pattern is documented and exercised by [Ikea's maintained filter
examples](../../../ikea/docs/filters.md); the catalogues here remain measurement controls.

## Comparisons

Every operation consumes sixteen original values and returns the complete mask:

- **inline_fused:** selected production filter inlined into the caller.
- **bound_fused:** the same operation behind one prepared function pointer.
- **one_stage:** that complete fused operation inside one existing CPS stage,
  followed by completion. This charges entry, context and completion costs.
- **shared:** physical-width decoder, canonical 1/2/4/8-byte comparator, completion.
  Relative to one_stage this exposes the incremental decode/compare boundary.
- **inline_decoded:** narrow SeriesPack control that reconstructs values before
  comparison, distinguishing algorithm choice from boundary cost.

The single-predicate panel covers all 64 SeriesPack widths and all five unsigned
comparisons with full and sparse activity. A second panel covers range predicates
(`x >= lo && x < hi`) at widths 3/8/12/24/40/64, plus TuplePack 5-bit shifted and
8-bit tight columns and a 5-bit strided projection. The range panel compares a
joined two-predicate stage with two separate comparison stages (`shared_split`).
Its fully fused baseline shares one decode, retaining direct packed evaluation
where the selected production policy uses it.

The mixed catalogue selects among 20 or 320 prepared width/predicate entries.
It compares bound_fused, shared, one_stage, and the candidate selective policy
(packed winners and standard widths fused, other operations shared). These are
same-binary route comparisons, not separate small-production-binary size tests.
Code address/layout, indirect branch targets and data selection all contribute;
a win here cannot be attributed solely to instruction-cache capacity.

## Method and limits

Pinned Clang 21.1.8, O3, explicit native bodies, loop and SLP vectorization disabled.
Zen 5 and Granite run AVX2 and AVX-512/GFNI profiles; V2 runs NEON.
The first Intel cohort accidentally used Zen 5 compiler tuning because its
environment override was outside `config.env`. Those retained runs are valid
common-binary controls. A corrected Granite-tuned cohort supplies the selected
Intel results below; `workers.json` is corrected for subsequent runs.
Each main case has five sequential repetitions (100,000 iterations for the width
sweep, 300,000 for range/TuplePack consumers). Focused rechecks use nine repetitions
of 1,000,000 iterations. Timed queries are in-domain. For SeriesPack, half are selected stored values
and half uniform generated values. TuplePack uses the same query generator
against a separate deterministic byte fixture; no fixed hit rate is asserted. Out-of-domain queries are checked for
correctness but do not have a timed workload here. The AVX-512 profile still uses
the existing sixteen-value carrier, including VL mask operations and GFNI; this
is not a claim about a 64-value ZMM traversal.

Candidate order is not randomized. Both dependent result-to-next-window latency
and eight independent recurrences report **ns per sixteen-value window**.
The complete result participates in address selection. Call entry/return,
query/result context traffic, branches, query loads and address mixing remain
inside the timing; binding/preparation is outside. Each independent stream has
its own mutable context. Results are consumed after timing.

These are warmed small-working-set consumers, not DRAM or Loom throughput claims.
A monomorphic fixture holds up to 8 KiB payload plus 512 B queries. The 320-entry
catalogue shares one 64-window fixture per physical width (266,240 B payload),
plus query, route and per-stream context storage. Its access trace is deterministic
but not uniform across entries. No cache-residency classification is asserted.

A separate Local encoder and scalar oracle test boundaries, sparse/empty activity,
all returned positions and full-width queries before native timing. TuplePack
expects values directly from physical input bytes. Generated measurement loops,
entry functions and native stages are inspected; no timing is accepted as a
claim of free boundaries merely because stages contain no ordinary calls.

## Decision

Adopt **selective sharing at an existing composition boundary**, with ordinary
hot filter calls remaining fully fused. The canonical 1/2/4/8-byte lane contract
is the useful seam: a physical decoder removes its source-width specialization
axis before a substantial shared consumer. Keep direct packed predicates with
their formats, and fuse neighboring cheap comparisons into one consumer stage.
The existing `filter_values` and native-chain facilities already express this;
there is no new public context ABI, registry or automatic dispatch policy.

This is a concrete choice against making every filter a shared CPS pipeline.
Entry/context/return costs are substantial for a sixteen-value operation. Once
CPS entry is already required, shared SeriesPack decode/compare often costs much
closer to one fused stage. That is a net complete-consumer difference, including
changed instruction scheduling and bounds tests, not an isolated branch latency.
Some combinations still lose; the broad sweep and mixed catalogues prevent a
single favorable kernel from setting a global policy.

Retain selective catalogue sharing as a supported authoring pattern, not a
threshold-based library preset. On Zen 5 the large-catalogue throughput gain can
justify its latency cost. The NEON catalogue loses both; it provides no reason
to switch its default. The earlier 63.7% text reduction and 40.7% compiler-RSS
reduction remain code-size observations from the separate controlled probe,
not size measurements of these multi-variant benchmark executables.

The ordinary filter kernels and public interfaces are unchanged by this runtime
qualification. `ikea/docs/source.md` and `ikea/docs/filters.md` now state the
selected composition policy. Hashtable remains parked.

Across K9–64, all five predicates and both activity masks (560 cases per metric
per profile), the net shared-minus-one-stage differences are below. These are
distributions over this case matrix, not application workload weights or
confidence intervals. P95 uses nearest rank; unstable observations remain in
the original matrix and are addressed by the focused reruns below.

| Platform | Latency median delta | Latency P95 delta | Throughput median delta | Throughput P95 delta |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | −0.71 ns | +0.41 ns | −0.24 ns | +0.67 ns |
| Zen 5 AVX-512 | −0.03 ns | +0.68 ns | +0.03 ns | +0.67 ns |
| Granite AVX2 | −1.34 ns | +0.49 ns | −0.48 ns | +0.35 ns |
| Granite AVX-512 | −0.08 ns | +0.44 ns | −0.09 ns | +0.59 ns |
| V2 NEON | +0.42 ns | +1.28 ns | +0.15 ns | +0.46 ns |

## Complete single-predicate consumers

Median **ns per sixteen-value window**, full activity. Latency is one dependent
chain; throughput is time per window across eight independent chains. Smaller
is better. These are complete consumer costs, including the address recurrence.

### SeriesPack K12, equality

| Platform | Metric | Inline fused | Prepared fused | One CPS stage | Shared stages |
| --- | --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | Latency | 10.25 | 10.70 | 12.56 | 12.52 |
| Zen 5 AVX2 | Throughput | 2.80 | 3.56 | 4.90 | 4.75 |
| Zen 5 AVX-512 | Latency | 7.62 | 8.22 | 10.09 | 10.09 |
| Zen 5 AVX-512 | Throughput | 1.07 | 1.81 | 3.59 | 3.37 |
| Granite AVX2 | Latency | 12.45 | 13.13 | 14.79 | 14.82 |
| Granite AVX2 | Throughput | 4.01 | 4.98 | 5.84 | 5.68 |
| Granite AVX-512 | Latency | 8.49 | 8.82 | 10.40 | 10.32 |
| Granite AVX-512 | Throughput | 1.50 | 2.14 | 3.75 | 4.45 |
| V2 NEON | Latency | 17.91 | 17.92 | 20.77 | 20.71 |
| V2 NEON | Throughput | 3.75 | 5.38 | 9.56 | 9.33 |

### SeriesPack K3, equality

| Platform | Metric | Inline fused | Prepared fused | One CPS stage | Shared stages |
| --- | --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | Latency | 4.53 | 4.66 | 6.57 | 11.69 |
| Zen 5 AVX2 | Throughput | 1.60 | 2.06 | 3.39 | 5.18 |
| Zen 5 AVX-512 | Latency | 6.65 | 6.91 | 8.74 | 8.74 |
| Zen 5 AVX-512 | Throughput | 0.99 | 1.59 | 3.59 | 3.37 |
| Granite AVX2 | Latency | 6.73 | 7.24 | 8.73 | 13.45 |
| Granite AVX2 | Throughput | 1.96 | 2.70 | 4.24 | 5.81 |
| Granite AVX-512 | Latency | 7.58 | 8.06 | 9.64 | 9.67 |
| Granite AVX-512 | Throughput | 1.24 | 1.88 | 3.92 | 4.46 |
| V2 NEON | Latency | 6.59 | 7.29 | 10.56 | 27.25 |
| V2 NEON | Throughput | 2.08 | 2.87 | 7.01 | 10.01 |

## TuplePack and adjacent predicates

The tight shifted 5-bit column is a useful counterexample to unconditional
normalization. Its selected filter compares encoded bytes directly. The shared
path instead pays for a general prepared projection before comparison.

| Platform | Latency fused | Latency shared | Throughput fused | Throughput shared |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 4.64 | 11.09 | 1.15 | 5.44 |
| Zen 5 AVX-512 | 4.88 | 9.95 | 1.09 | 4.82 |
| Granite AVX2 | 4.90 | 11.80 | 1.55 | 7.20 |
| Granite AVX-512 | 5.40 | 11.96 | 1.46 | 6.59 |
| V2 NEON | 11.39 | 16.04 | 2.80 | 10.28 |

Strided and sparse projections are also measured, across all five predicates.
They usually bring shared-stage latency closer to a single CPS stage, but do not
make throughput free: Granite AVX2 sparse shifted equality costs 9.74 ns/window
with one stage and 12.56 with sharing. The projection's emitted branches and
register pressure belong to the complete comparison.

The range consumer decodes once and evaluates `lo <= x && x < hi`. Splitting its
two cheap comparisons into separate stages gives no consistent latency benefit
and usually adds throughput cost. At K12:

| Platform | Latency joined | Latency split | Throughput inline fused | Throughput joined | Throughput split |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 13.99 | 14.04 | 3.27 | 5.78 | 5.86 |
| Zen 5 AVX-512 | 10.31 | 10.45 | 1.29 | 4.07 | 4.51 |
| Granite AVX2 | 15.72 | 15.82 | 4.97 | 6.88 | 6.97 |
| Granite AVX-512 | 10.97 | 11.03 | 1.76 | 4.43 | 5.30 |
| V2 NEON | 21.31 | 21.02 | 5.20 | 11.25 | 11.18 |

## Mixed catalogue

With 320 width/predicate choices, selective sharing can improve throughput even
though it adds dependent latency. The same binary contains all alternatives;
this is not a clean experiment isolating instruction-cache footprint. Corrected
Granite tuning yields a modest throughput gain in both ISA profiles; Zen 5
retains the larger gain. NEON still loses throughput and latency.

| Platform | Latency prepared fused | Latency selective | Throughput prepared fused | Throughput selective |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 28.38 | 31.82 | 13.90 | 11.19 |
| Zen 5 AVX-512 | 25.06 | 30.00 | 12.67 | 8.07 |
| Granite AVX2 | 34.75 | 37.30 | 17.44 | 16.39 |
| Granite AVX-512 | 30.30 | 34.32 | 14.03 | 12.92 |
| V2 NEON | 40.91 | 44.49 | 15.77 | 18.10 |

The 20-entry catalogue favors prepared fusion on every profile in both metrics.
Its results and all shared/one-stage alternatives are retained, not just the
large-catalogue wins. There is no measured universal catalogue-size crossover.

## What the generated code establishes

The [audit summary](evidence/code-audit.json) identifies the archived disassembly
by hash. In the SeriesPack panel it checks 355 inline measurement functions and
404 native-stage bodies per profile. The second panel checks 16 inline consumers
and 40 stage bodies. No unexpected outlined calls appear in the inline loops;
no ordinary calls appear inside stages. Result masks feed the multiply/high-bit
address recurrence in the measured loops on all three ISAs.

That does **not** mean no stack or no boundary cost. Native entry saves/restores
registers and the query/result context crosses the ordinary/CPS boundary.
Stage handoffs load a continuation and tail-branch; comparators read their query
from context. Selected narrow SeriesPack AVX2 fused stages (K5 ordered and K6/K7 equality,
depending on tuning) have a few scalar spills. The x86 TuplePack prepared projection and fused fallback bodies
contain many stack references across their possible layout/mask branches. Those
are static observations, not counts executed per call. NEON stage bodies in
these panels contain no stack accesses. The timings charge whichever branches
actually execute.

A control that decodes K3 inline separates representation loss from dispatch.
On V2 its equality latency is 23.62 ns/window, versus 6.59 for the selected packed
filter and 27.25 for shared decoding/comparison. Most of that gap is throwing
away the direct bitplane algorithm, not a slow register handoff. The same issue
appears in tight TuplePack encoded-column comparisons.

## Variation and checks

All native pre-timing scalar-oracle checks passed. A focused local NEON
AddressSanitizer/UndefinedBehaviorSanitizer run also passes canonical contexts,
joined/split range stages at K3/12/64, and tight/strided shifted TuplePack equality.
It reuses the previously instrumented libraries; it is not another all-width
sanitizer sweep. The existing native-chain entry shim explicitly excludes
sanitizer instrumentation, while its caller and stage bodies remain instrumented. The full comparison covers
empty and sparse activity, domain boundaries and full-width query operands.
Longer focused reruns investigate isolated unstable measurements rather than
silently dropping them: V2 K55 `<=` shared throughput originally spans
16.62–35.63 ns/window (median 31.38); nine longer samples give
16.62–16.64 (median 16.63), versus 16.45 for one fused CPS stage. K60 `<` retains
its dependent penalty: 25.57 shared versus 23.96 one-stage. These focused binaries
have different placement from the full catalogue, so they qualify the unstable
observation without replacing the full-matrix rows. Zen5 AVX-512 K43 `<` also
had an unstable one-stage sample (30.71 ns latency median, 20.75–35.35 range).
Its longer rerun is 13.59 ns one-stage versus 14.15 shared; throughput is 4.52
versus 4.47. Neither large apparent outlier is used as a boundary-cost claim.

## Evidence and reproduction

The complete width/predicate/activity sweep is retained because the choice must
work beyond one kernel. Both winning and losing alternatives, individual repeats,
oracle output, host context and verified full-archive recovery references stay
in each export. Full disassembly and frozen sources are recovered from the
archive instead of being copied into Git.

The superseded Intel common-binary timing samples are archive-only. Their check
receipts, host metadata and recovery references remain in Git so they cannot be
mistaken for the selected Granite-tuned measurements. Recover either archive to
inspect its AVX2/AVX-512 CSVs. The selected five-profile sweeps retain every case
and repetition, including losses; the focused reruns retain the evidence needed
to resolve instability without overwriting the original observations.

- [Selected Granite-tuned panels](evidence/granite-tuned) supply all Intel table rows.
- First width/catalogue panel: [Zen5](evidence/runtime-zen5),
  [V2](evidence/runtime-neon), and [Intel common-binary control](evidence/runtime-granite).
- Range/TuplePack panel: [Zen5](evidence/extra-zen5), [V2](evidence/extra-neon),
  and [Intel common-binary control](evidence/extra-granite).
- Focused rechecks: [V2](evidence/recheck-neon), [Zen5](evidence/recheck-zen5).

For example, regenerate medians/ranges for all Zen5 AVX2 cases:

```sh
orb -m ubuntu python3 workbench/spikes/filter-composition/report.py \
  zen5-avx2=workbench/spikes/filter-composition/evidence/runtime-zen5/avx2.csv \
  zen5-avx2=workbench/spikes/filter-composition/evidence/extra-zen5/avx2.csv \
  --output build/filter-composition/zen5-avx2-summary.csv
```

Recover the exact source/disassembly for that run:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/filter-composition/evidence/runtime-zen5 \
  build/recovered/filter-composition-runtime-zen5
orb -m ubuntu python3 workbench/spikes/filter-composition/audit.py \
  build/recovered/filter-composition-runtime-zen5/avx2-disassembly.txt \
  build/filter-composition/recovered-audit.json
```

`native.sh`, `extra-native.sh` and `workers.json` describe fresh native runs;
`both-native.sh` runs both panels with separate output directories. `recheck-native.sh`
uses longer focused cases. These measure the current checkout; original archives
preserve each measured source cut. Local correctness can use `--check` on the two
main executables or the smaller `filter_composition_sanity` target. Preparation is
outside these measurements; no allocation, suspension, DRAM benefit or general
pipeline application speedup is inferred from them.

## Module integration checks

The maintained filters and composition examples are registered with
`ikea_validate`. On 2026-10-15, all 34 Ikea checks/examples passed in the local
NEON build. The complete sharded filter check and both examples also passed with
ASan/UBSan and on native Zen 5 AVX2/AVX-512, Granite Rapids AVX2/AVX-512 and
Neoverse V2 NEON. Coverage includes all SeriesPack widths and predicates,
canonical lane normalization, every TuplePack code width/shift, tight/strided
placement, grouped packet coordinates, holes and invalid activity masks.

The native jobs are `20261015T140351Z-3621cdb3` (Zen 5),
`20261015T140351Z-207c2db9` (Granite), and `20261015T140351Z-b208255a` (V2).
Recover sources, checks and disassembly with `workbench/tools/worker.py fetch JOB`;
[`ikea/test/filters/native.sh`](../../../ikea/test/filters/native.sh) reruns those
checks. Each example executable has two decoders and one shared filter/count
stage, without ordinary calls or stack accesses inside those stages. The native
entry shim's existing sanitizer exclusion still applies. These are integration
checks, not new timing measurements; the selected kernels and calling convention
remain those qualified above. Routine successful logs and earlier failed example
builds stay in their worker archives rather than adding more Git exports.
