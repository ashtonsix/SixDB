# Early rejection in packed filters

[Findings and recommendation](EARLY-RESULTS.md) contain the native measurements,
negative results, code-generation audit and recovery references.

Question: when does skipping remaining comparison work repay a mask reduction
and a branch? Compare complete current filters with independent fused kernels,
branchless progressive comparisons, and the same comparisons with early exits.
Latency (one result-dependent address chain) and reciprocal throughput (eight
independent chains) remain separate, in ns per sixteen-row window.

The experiment covers narrow bitplanes, ordinary Local byte/tail layouts,
existing separate-eight-bit-head layouts, and two-predicate TuplePack consumers.
Use all five unsigned predicates, representative widths through 64, and data
that separates result selectivity from unresolved-prefix frequency. Retain
always-resolved, shared-prefix, all-equal, one-match, random and mixed easy/hard
windows. Mixed cases test branch costs; a zero final mask alone does not imply
an opportunity to stop early. No new per-block summary metadata is assumed.

All variants operate on the same bytes within a layout. Head layout comparisons
also retain their ordinary filter baseline: changing layout is not an early-exit
gain. Explicit SIMD, pinned Clang, no automatic loop/SLP vectorization. Native
Zen 5 and Granite AVX2/AVX-512 plus Neoverse V2 NEON. Independent scalar truth
and a wire encoder check complete masks before timing; inspect generated branches
and complete timed consumers before interpreting results.

The original exploration left production unchanged. The selected head-first path
has since [graduated through composition](EARLY-GRADUATION.md); default filters
and hashtable remain unchanged. The measurements below retain their original
source identity.

## Implemented comparisons

SeriesPack widths: 1, 2, 3, 5, 7, 8, 9, 12, 15, 16, 24, 31, 32, 40,
48, 56, 63 and 64. Every width uses the Local layout; widths above eight
also use the existing separate eight-bit head. There are five predicates and
ten patterns for every format.

| Variant | Local layout | Separate head |
| --- | --- | --- |
| current | Production complete filter | Production complete filter, same head layout |
| fused | Independent native comparison at standard widths; independent byte/plane comparison otherwise | Independent head comparison plus independent suffix comparison |
| progressive | Independent most-significant-byte/plane comparison, no early stopping | Head comparison plus production suffix filter, always evaluating the suffix |
| early | Same progressive comparison, stopping when no unresolved lanes remain | Same split comparison, skipping suffix when no head ties remain |
| gate / fused_early | Test leading byte/plane; fall back to the current complete filter | Guard the independent fused suffix comparison |

The `early` versus `progressive` pair isolates adding stopping branches to an
otherwise identical algorithm. Comparisons against `current` and `fused` ask
whether that algorithm is competitive at all. For ordered predicates, lanes
already known smaller are accumulated separately; a zero unresolved mask can
return a mixed result, not just an all-zero result. No per-block min/max summary
or extra stored metadata is introduced.

The wide AVX-512 follow-up adds full ZMM comparisons for Local K32/K64 and the
32-bit suffix of head8 K40. This strengthens the independent baseline beyond
the broad panel's inherited AVX-512 VL control. The independent nonstandard
byte/plane control is an alternative algorithm, not a claim that every possible
unpacking kernel has been exhausted; the production filter remains a separate
competitor throughout.

TuplePack widths 1/3/5/8 use tight byte columns and stride-four columns, with
neighbor bits populated. The consumer applies the selected first predicate and
an independent second-column `< 128` predicate. It compares two unconditional
production filters, an explicit empty-result guard, forwarding the first mask
to the second prepared filter, and layout-specialized fused SIMD consumers with
and without a guard. This distinguishes avoiding the entire second projection
from changing how a nonempty sparse projection executes.

## Inputs, measurement and limits

`uniform`, `ordered1` and `ordered99` use generated uniform values; the latter
two put the query near the domain's first/99th percentiles. Narrow domains
quantize those selectivities. `reject_first` changes the most significant bit
of every value relative to the query. `shared_prefix` preserves the leading
byte (leading bit below width eight) but varies the suffix. `one_match`,
`half_match` and `all_equal` give equality selectivity 1/16, 1/2 and 1.
`mixed10`/`mixed50` randomly choose shared-prefix windows among otherwise
immediately resolved windows. Prefix ambiguity, predicate result selectivity
and branch predictability are distinct quantities.

Each fixture has 256 sixteen-row windows, generated deterministically without
using Ikea's writer. Paired layouts encode the same logical values. Payload plus
query storage is at most 34 KiB per SeriesPack fixture. This is a warmed kernel
experiment, with no cold-cache, DRAM, Loom or application speedup claim.

The full result mask and previous cursor feed a multiply/XOR address recurrence.
Each independent stream has its own salt, preventing a stream collision from
permanently merging its subsequent query sequence with another stream. An
earlier unsalted throughput cohort was superseded before choosing a policy.
Dependent timing uses one chain; reciprocal throughput uses eight separately
unrolled chains and divides elapsed time by eight times the iteration count.
Both report ns per complete sixteen-row operation, including query loads,
address recurrence and branches. Lower throughput ns means more operations per
second; it is not per-operation latency. Preparation and oracle checks are
outside timing. All variants return identical masks and therefore follow the
same addresses. Cases and repeats run sequentially, without interleaving.

The CSV's selection and first-resolution fractions come from a separate
32,768-step dependent trace. They describe the input/branch opportunity, not
hardware counters or exact counts from each timed repetition. All timed
queries are in-domain and activity is full. Empty/sparse input masks and
out-of-domain queries are correctness cases; this experiment measures
data-dependent stopping rather than assigning an uplift to existing admission
shortcuts. TuplePack fractions describe the complete pair and the first
predicate's empty-mask frequency.

The full sweep uses five repeats of 30,000 iterations. Focused rechecks use seven
repeats of 500,000 iterations; the ZMM control uses seven of 300,000. Keep both
latency and throughput for winning and losing cases. A branch that wins on
predictable rejection does not establish a default for correlated or mixed data.
The same finite fixtures recur, so predictor training is part of the warmed
measurement. In particular, mixed-window dependent times improve over successive
repetitions. The results retain every sample and use the longer rechecks for
headline comparisons; no cold-predictor or universal selectivity threshold is
claimed.

The compiler is Clang 21.1.8, O3 without debug information for these large
measurement binaries, with loop and SLP vectorization disabled. Native profiles
are Zen 5 AVX2/AVX-512, Granite Rapids AVX2/AVX-512, and Neoverse V2 NEON.
The AVX-512 profile admits GFNI and VL as well as full-width instructions;
the ordinary sixteen-row carrier is not automatically a ZMM traversal.

The build is sharded by width/predicate to bound compiler memory while leaving
the eight-stream measured loops fully inlined. That is build organization,
not a runtime dispatch seam. Initial oversized translation units and an Intel
Spot interruption did not produce qualifying native timings. The replacement
cohort uses on-demand workers.

## Reproduce

`early-native.sh` and `early-workers.json` run the full native matrix.
`early-focus.sh` runs longer paired rechecks, full ZMM controls on x86, and
attempts whole-process branch counters when `perf` is available. Its counters
include setup/checking and both timing modes; they are not loop-only counters.
`early-qualify.sh`, selected by `early-workers.json`, runs both phases.
`early-zero.sh` separately tests a SIMD zero reduction before materializing the
equality mask, for head8 K12/K64, equality/less-than and five patterns, with seven
500,000-iteration repetitions. Its native controls are rerun in the same binary.
`early-report.py INPUT.csv OUTPUT.csv` preserves all repetitions, medians and
ranges, with current/fused/progressive references in each row.

The `packed_filters_early_guards` target checks bounded loads and unreadable
suffixes. The local guard build also uses ASan/UBSan. Full-mask scalar-oracle
checks precede timing for every measured variant. Full disassembly stays with
the recoverable runs; useful findings and selected evidence belong below.
