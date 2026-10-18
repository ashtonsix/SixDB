# Guarded filters in Ikea

The selected head-first comparison now lives in
[`seriespack/author/filter.h`](../../../ikea/include/ikea/seriespack/author/filter.h).
An author chooses `composition::guarded(expression)` after composing an
eight-bit head with its payload. The existing sixteen-row, window and prepared
filter operations consume that expression. Ordinary defaults, formats and
stored metadata are unchanged. The [production guide](../../../ikea/docs/filters.md)
owns the contract and the [guarded example](../../../ikea/examples/filters/guarded.cpp)
is maintained under `ikea_validate`.

The selected body tests head equality before reading the payload. Ordered
predicates retain the lanes already decided by the head. NEON uses a vector-zero
reduction for full activity and a masked lane reduction for sparse activity;
AVX2 uses the scalar mask and AVX-512 uses native mask-register testing. Surviving
groups use the ordinary payload comparison. This follows the actual substituted
expression and the head owner's geometry, including gaps. Unaligned edges retain
ordinary scalar evaluation. The wrapper owns expression nodes and borrows named
views; admission and ordinary value reads still visit the selected leaves.

For TuplePack conjunctions, the consumer guards the next already-admitted filter
with `if (selected)`. The example keeps the original activity for the surviving
projection and intersects its result, avoiding an accidental switch to the
sparse reader. No new TuplePack mode or conjunction registry is introduced.
The [original measurements](EARLY-RESULTS.md) explain why this remains a consumer
choice rather than a universal branch between predicates.

## Production qualification

`early-graduate.sh` runs all existing filter checks and the ordinary, shared-stage
and guarded examples before measuring. Its native profiles are Zen 5 and Granite
Rapids AVX2/AVX-512 and Neoverse V2 NEON, with the pinned Clang 21.1.8, O3 and
automatic loop/SLP vectorization disabled. The source capture is shared across
the three hosts.

The benchmark compares the production `guarded` composition with current,
branchless progressive, spike-guarded and independent fused consumers for head8
K12/K40/K64, all five predicates and uniform/rejected/shared-prefix/mixed/equal
windows. It also reruns the independent vector-zero control for equality and
less-than, and full-ZMM K40 suffix controls on AVX-512. All complete masks are
checked independently before timing.

Seven 500,000-iteration repetitions measure dependent latency and eight-stream
reciprocal throughput separately, using the corrected stream salts from the
exploration. Units are ns per sixteen-row operation, including query loads and
address recurrence. `graduated` retains a general bound view with runtime strides.
`graduated_inline` exposes contiguous placement inside the inlined consumer,
as the independent controls do. Both call the same production body through the
same public composition entry. Their difference includes placement/addressing
cost, not merely the branch or the expression annotation; the latter is not an
additional API or preset. Fixtures remain warm and finite; the same predictor-training and residence limits apply as in the
[exploration method](EARLY-EXITS.md).

The all-width regression now exercises the opt-in path for every supported
nonempty head8 width. Additional checks use independently encoded bytes, unreadable
retired payloads, unreadable live suffixes behind resolved heads, inactive head
ties, gapped/striped layouts, source ends against guard pages, out-of-domain
queries, partial windows and invalid extents. Local ASan/UBSan checks passed.
All five final native profiles passed the all-width regression, all three filter
examples and the benchmark oracle. The
production header, new guarded tests, maintained example and benchmark consumer
match the final captured source.

## Measured outcome

Every cell below is **dependent latency / reciprocal throughput**, in ns per
sixteen-row operation; lower is better. These are medians, not selected fastest
samples. Each retained summary includes all seven samples, minimum and maximum.

For headed K64 equality with uniformly distributed inputs, a cheap head usually
rejects the whole group. The production path retains the useful early-exit gain:

| Platform | Current | Independent guarded control | Guarded, bound view | Guarded, placement visible |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 9.60 / 1.92 | 1.76 / 1.11 | 1.88 / 1.36 | 1.73 / 1.06 |
| Zen 5 AVX-512 | 9.96 / 1.96 | 2.05 / 1.11 | 1.85 / 1.43 | 1.79 / 1.10 |
| Granite AVX2 | 10.77 / 3.72 | 2.55 / 1.47 | 2.89 / 1.73 | 2.37 / 1.33 |
| Granite AVX-512 | 11.64 / 4.54 | 2.39 / 1.34 | 2.59 / 1.77 | 2.31 / 1.35 |
| V2 NEON | 17.48 / 7.33 | 2.86 / 1.72 | 3.08 / 1.98 | 2.93 / 1.81 |

The independent control is `vector_zero`, with explicit SIMD and its own packed
consumer. The reports also retain the independent unconditional `fused` and
guarded `fused_early` implementations; this table does not substitute for those controls.
The remaining bound-view cost is measurable, especially in throughput. Making
placement visible removes much of it without another public entry or format.

Mixed easy/hard windows remain a counterexample. The branch can improve latency
while losing reciprocal throughput to the unconditional current implementation:

| Platform | Current | Guarded, bound view | Guarded, placement visible |
| --- | ---: | ---: | ---: |
| Zen 5 AVX2 | 9.77 / 1.95 | 6.50 / 6.68 | 6.19 / 5.97 |
| Zen 5 AVX-512 | 9.97 / 1.98 | 6.95 / 6.69 | 6.66 / 6.12 |
| Granite AVX2 | 10.77 / 3.71 | 9.79 / 7.42 | 8.76 / 6.50 |
| Granite AVX-512 | 11.63 / 4.54 | 11.24 / 7.77 | 9.91 / 6.80 |
| V2 NEON | 17.63 / 7.33 | 15.41 / 9.59 | 13.83 / 9.09 |

Narrow suffixes also constrain the opportunity. On K12 uniform equality, Zen 5
AVX-512 current throughput is 1.03 ns versus 1.40 ns for the placement-visible
guard; Granite is 1.46 versus 1.65 ns. For K40 uniform equality, the independent
full-ZMM unconditional consumer reaches 0.97 ns on Zen 5 and 1.23 ns on Granite,
versus 1.51 and 1.79 ns for the placement-visible production guard. The guard's
latency is lower in those cases, but it is not the throughput winner. These
tradeoffs are why ordinary defaults remain unchanged and selection belongs to
the complete consumer.

No general per-plane guard or arbitrary Local prefix extraction was graduated.
The selected eight-bit-head expression has a cheap, already separated decision
point, follows the existing substitution model and needs only one annotation.
TuplePack's useful predicate-boundary guard needs no annotation at all. The
broader width/selectivity and TuplePack results remain in
[the exploration report](EARLY-RESULTS.md), including losing branches.

## Native code inspection

The audit examines actual timed consumers: 111 measurement functions per AVX2
or NEON profile and 121 per AVX-512 profile. All five report zero unexpected
calls. This establishes inlining of the filter bodies, not absence of spills or
perfect scheduling. Timing includes the resulting machine code.

The final placement-visible K64 equality consumers were inspected directly:

- Zen 5 AVX2 compares head bytes with `vpcmpeqb`, extracts with `vpmovmskb`,
  and branches on `test` before any suffix work.
- Zen 5 AVX-512 uses `vpcmpeqb` into a mask register and `kortestw` before the
  suffix branch.
- V2 NEON uses `cmeq`, `umaxv`, `fmov` and `cbnz`; the rejected path avoids
  scalar lane-mask reduction and all suffix work.

The reused Local head reader preserves its two eight-row tiles. On NEON this
still emits two 64-bit loads and a lane insert, where the independent flat
control can use one 128-bit load. That leaves a small measurable cost, for
example K64 less-than uniform throughput of 2.84 ns for placement-visible
production versus 2.53 ns for the independent vector-zero control. Retaining
one reader that also handles gaps and substitutions is the chosen tradeoff;
these results do not establish universal parity with hand-fused consumers.

The disassembly retains the complete result mask and the result-dependent
next-address recurrence. Native measurements, not static instruction counts,
qualify branch behavior. Finite warm fixtures and sequential repetitions allow
predictor training: no cold predictor, DRAM or Loom throughput claim is made.

## Evidence and reproduction

The final shared capture is worker group `20261015T170354Z-37f04e19`:

| Host | Job | Retained evidence |
| --- | --- | --- |
| Zen 5, c8a.large | `20261015T170402Z-0078ff4f` | [AVX2 and AVX-512](evidence/early-graduate-zen5/) |
| Granite Rapids, c8i.xlarge | `20261015T170408Z-5ea0d53c` | [AVX2 and AVX-512](evidence/early-graduate-granite/) |
| Neoverse V2, c8g.large | `20261015T170414Z-a7d11c70` | [NEON](evidence/early-graduate-neon/) |

Each directory retains host/job receipts, successful checks, oracle output,
summary CSV and the disassembly audit. Its `provenance.json` hashes every selected
file; `artifact.json` identifies the verified full archive containing raw samples,
binaries and disassembly. Job receipts identify the frozen source archive.
All retained file hashes and summary statistics were checked. This final cohort
supersedes the earlier qualification runs; headline comparisons use only this
cohort.

[`early-graduate.sh`](early-graduate.sh) is the native build/check/measure runner.
Use [`early-workers.json`](early-workers.json) with its top-level
`script` changed to `workbench/spikes/packed-filters/early-graduate.sh`; keep the
platform tuning, CPU pinning and worker environment. The retained `job.json`
files give the exact submitted configuration. Recovery follows the shared
[artifact guide](../../tools/artifacts.md). Earlier exploration artifacts remain
separate so their measurements are not silently combined with this recheck.
