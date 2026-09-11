# Historical findings during the rebuild

This historical account precedes the consolidated candidate snapshot. See the
[current evidence entry](README.md) for its disposition. These are captured
measurements, not a declaration that SeriesPack is delivered.
Each evidence directory retains compact samples, comparisons and source/host
provenance. Its `artifact.json` recovers the source snapshot, binaries and raw
results. Regenerate a report with `python3 ikea2/bench/summarize.py EVIDENCE`.

## Comparison current at the earlier write-up

The [Zen run](evidence/20260911-body-packets-zen5/summary.md) and
[ARM run](evidence/20260911-stripe-insert-v2/summary.md) include ordinary
reads, 76 full-overwrite formats, selected sum consumers, replacement maintenance,
and mutation-ending continuations. Each uses sequential pinned repetitions.
Ratios compare per-case medians; these are warm microbenchmarks, not whole queries,
page faults, publication, buffer acquisition or scheduling.

| Operation | Zen5 AVX-512 | Neoverse V2 NEON |
| --- | --- | --- |
| Ordinary range `get16`, endpoint-consuming sink, 15 same-wire cases | median 1.108×; worst 1.383× | median 1.077×; worst 1.465× |
| Admitted `read16`, same sink and cases | median 0.910×; worst 0.996× | median 0.959×; worst 1.116× |
| Bulk decode, 15 specialized same-wire cases | median 1.144×; worst 1.385× | median 1.018×; worst 1.451× |
| Bulk decode, 61 further cases against existing Ikea | median 1.020×; none over 1.4× | median 1.014×; one over 1.4× |
| Raw full overwrite, 76 formats | median 1.012×; none over 1.4× | median 1.160×; four over 1.4× |
| Native selected sum, 14 cases against strongest recorded control | median 0.554×; worst 1.178× | median 0.558×; worst 1.060× |
| Replacement with sum and coverage, 18 cases against equivalent materialization | median 0.299×; worst 1.211× | median 0.558×; worst 0.970× |

The endpoint-consuming `get16` sink matches the earlier campaign. The separate
all-lanes sink dilutes boundary costs and cannot replace that comparison. Native
sum controls include the previous grouped/deferred Local12 and Local31 consumers,
where available, not merely materializers. Maintenance controls decode old values,
apply the same selected replacement and sum delta, record coverage, then encode.
Coverage-only rows compared with raw encoders do additional work; inspect each
row's actual named control rather than conflating these ratios.

The broad primitive median failure of the earlier implementation is no longer
present on either machine. The latest Zen matrix has no decode or raw-overwrite
case above 1.4×. ARM retains four raw-write cases above that line (worst 1.508×),
plus isolated read cases. Compound sum and maintained replacement results are
strong on both machines. Coverage-only operations need their own matched control:
the existing table compares them with raw encoders and shows a substantial cost
for repeated coverage reporting in striped traversal.

## Changes that earned their place

- Complete-tile traversal makes striped group choices constant. Narrow striped
  Zen bulk ratios fell from approximately 7–12× in the early rebuild to within
  1.3× in the latest same-wire matrix. Short random reads retain their own grain.
- Compile-time callbacks must remain inline at their invocation too; outlining
  them lost constants and put captures in memory. This was a substantial writer
  regression, not an inherent composition cost.
- Mutation freezes placement metadata at entry, avoiding reloads caused by byte
  stores aliasing borrowed view metadata. Full stripes are assembled once, and
  dense Local tails batch eight packets. Raw nine-case overwrite medians fell
  from 2.312× on Zen to approximately parity.
- One-bit encoding needs a bit-mask/weighted-sum realization, not a full transpose.
  The wider overwrite sweep caught 2.5× Zen and 10× ARM outliers omitted by the
  first nine cases. The latest implementation removes those large outliers.
- Dense Local coverage can be recorded once per contiguous nonempty run. Its
  granularity need not follow packets, native registers or summary finalization.

The [full-spectrum ARM experiment](evidence/20260911-full-spectrum-v2/summary.md)
that recursively widened native values was rejected: 32 of 61 additional decode
cases exceeded 1.4×. The latest completed run restores single table projections.
A single doubling instruction remains useful for one-step widening; longer
ratios use table projection. Moving mixed Local residual transposition to integer
registers then reduced the 61-case ARM decode median from 1.35× to 1.011× and
removed the substantial Local12/31 consumer losses. This keeps vector execution
available for body expansion and joining.

Exact-width stores removed the large Zen full-write maintenance losses: Local31
fell from 2.968× to about 0.665× equivalent materialization. Replacing masked
64-byte stores for smaller packets is consistent with avoiding interference with
following loads; hardware counters have not isolated that mechanism. Native
32-row striped execution removed the corresponding mixed-stripe read/write gaps.
Expanding a complete non-power-of-two byte body with AVX-512 byte permutation
then removed the seven remaining wide Local decode cases above 1.4×. Short
composition and bulk execution share that reconstruction.

ARM one-bit residual writing now uses weighted bit extraction in both standalone
and nested contexts. Whole stripes with one-, two- or four-bit fields use native
shift-insert operations. The raw-overwrite median fell from 1.208× to 1.160× and
the count above 1.4× fell from twelve to four. These are family-level changes,
not per-width dispatch exceptions.

## Continuations and integration

The same authored filter and mutation bodies run inline, through straight-through
native continuations, and through ordinary scratch-array calls. A manually fused
arm precombines the equivalent cutoffs. This proves body reuse and preservation of
values, masks and effects; it is not an isolated tail-call-overhead measurement.
There are six cases: widths 7, 12, 31 and 64 at depth three, plus widths 7 and 31 at
depth seven, on 16-row native carriers. CPS/inline ratios are median 1.549× and
worst 3.547× on Zen, and median 1.198× and worst 1.322× on ARM. Compact comparison
evidence and `preserve_none` reduced absolute Zen CPS times, but improved inline
execution still more. For seven-stage width7, CPS fell from about 1.269 to 1.116
ns/value while inline fell from 0.540 to 0.312. These deliberately tiny stages
expose a poor amortization point; the larger ratio must not be hidden by the
absolute improvement. A general continuation grain still needs evidence.

The [integration example](../../../../ikea2/integration.md) tests owned waits, rotation, stale
completion, pre/post-submission cancellation, conflicts and coordinated visibility
of data and safe summaries. A second adapter mutates one active buffer across a
stop. It requires no immutable candidate and accumulates repeated-write deltas
from current values. Orbital will preserve versions through UFFD page COW;
Ikea's byte-write reporting is independent of that mechanism. The examples are
serialized ownership tests, not an implemented concurrent Loom/Engine/Orbital
runtime or a UFFD performance result.

## Unsettled scope

Functional checks cover all 206 current placements, independent wire oracles,
inactive values, exact issued-byte coverage, gaps, slack and rejection before
side effects. Nested writes replace children across three owners and keep retired
storage inaccessible. The new writable-expression admission and erased bounded
invocation also check actual substituted destinations; their conservative alias
rule is documented with the implementation.

Still needed: broader performance evidence for AVX2 and headed/placed consumers;
ordinary mutation and construction binding; representative CPS operating points; richer sparse/progressive consumers;
a small curated preset surface and persisted format descriptions. The continuation
ABI is provisional, not a stable external or persisted interface. Engine owns
schema, representation choices per segment and publication; Loom owns acquisition
and scheduling. Existing Ikea's C++ API does not constrain this candidate.

## Compilation and output size

Zen job `20260911T105725Z-ea6ab0f7` stopped when Clang was killed compiling
`mutation_low.cpp`, concurrently with `mutation_medium.cpp`, on a four-vCPU,
8-GiB worker with two build jobs. Both TUs instantiated sixteen widths across
several layouts. The failed configure/build script consumed 150.70 seconds;
reported maximum process RSS was 4,134,720 KiB. This is consistent with concurrent
compiler memory exhaustion; no retained kernel OOM record establishes the kill's
cause conclusively. Tests and benchmarks did not start.

The serial incremental retry `20260911T110321Z-735862e3` succeeded in 292.20 seconds
for the entire configure/build/check/benchmark script, with maximum process RSS
4,173,516 KiB. Its isolated build time was not recorded. Neither process maximum
measures aggregate concurrent memory. The runner now captures configure/build
times separately and retains the completed Ninja edges attributable to that
invocation, including on failure. Failed edges have no completed-edge timing.

The successful retry's [size breakdown](evidence/20260911-group-maintenance-zen5/code-size.json)
separates machine code from debug information. Its validation executable was
80.45 MiB unstripped, 12.47 MiB stripped, with 11.57 MiB of code and 67.27 MiB
of debug sections. The benchmark was 61.80 MiB unstripped and 7.05 MiB stripped;
it contains both implementations and comparison controls. The Ikea2 compiled
reader archive contains 378.9 KiB of code and 30.6 KiB of read-only data; its
4.88-MiB file is predominantly debug information. This archive covers compiled
ordinary readers, not every possible application instantiation or mutation
composition, and is not coverage-equivalent to the old Ikea archive.

Mutation validation now uses eight-width TUs and one compiled byte-coverage
oracle. A following change shares the test selection type and operation calls
across scenarios, avoiding repeated forced-inline range writers in assertion
drivers. Both changes preserve optimized kernel bodies and the full set of
format/mask/rejection cases. The following measurements distinguish peak memory, serial compile work and
linked size; splitting alone would not establish a reduction in all three.

The [test reuse run](evidence/20260911-test-reuse-zen5/build-summary.json)
rebuilt the affected Zen test objects with two jobs in 75.48 seconds. Its maximum
process RSS was 1,411,268 KiB. Serial compiler probes of widths 1–8, 9–16 and
17–24 totalled 70.364 seconds, compared with 113.417 seconds for those same three
serial Ninja edges before test-call deduplication (about 38% less). The diagnostic
probe redirects object outputs and does not control OS cache state; its peak was
1,411,764 KiB. The validation executable's code shrank from 11,877,418 to 11,318,826
bytes, and its unstripped file from 84,503,280 to 74,627,416 bytes. Both versions
passed the functional/integration checks. The entire Zen benchmark binary is
byte-identical across this test-only change, SHA256
`5b5e6a112bd26c73ba553d84e1492610bdb8431dbb41eab37120c8e57ad0223f`.
See the retained `compile-cost.json` and `code-size.json` for the full breakdown.
