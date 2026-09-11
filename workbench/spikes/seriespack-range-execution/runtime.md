# Runtime ranges across the scalar callback ABI

The scalar callback improves all **68 runtime-range cases** measured on Zen 5:
median CPU time falls **10.3–66.1%**, every before/after raw sample range is
separated, and both ABBA edges agree. The gain survives unaligned origins,
runtime 16/17 counts, u32/u64 sinks and independently strided payload/heads.
Boundary lowering still determines much of the remaining cost. This extends
the [fixed-16 callback finding](../ikea-composition/call-boundaries/decoder.md); it does not establish
an arbitrary-range predecessor comparison or cold-memory result.

The [GNR follow-up](granite-rapids.md) also improves all 68 runtime
cases, by 5.86–61.13%. The [V2 follow-up](neoverse-v2.md) is mixed:
19/34 runtime medians improve, with 17 separated improvements and 12 separated
losses. The Zen result below does not generalize across architectures.

## Measurement and provenance

Job `20260910T203524Z-5809581c` ran on a newly launched c8a.large, CPU 0,
Clang 21.1.8, Zen 5 tuning, x86-64-v4 plus VBMI/VBMI2/GFNI. The AVX2-labelled
endpoints share that full profile; they are not AVX2-only builds. Four sequential
blocks ran before/after/after/before, three repetitions per case per block,
0.03 s minimum. Ratios use the median of six raw `cpu_time` samples per variant.
Ranges are raw extrema, not confidence intervals. Divide a runtime/fixed-16
iteration by 256 queries and a bulk iteration by 8,192 values.

The [audit and recovery record](../ikea-composition/archive/validation-20260911.md) verifies the 490
source files against the capture, worker manifest and returned source archive;
the exact three-file ABI delta; both measured executable hashes; 35 fresh
objects per variant; all commands and correctness checks; exact inventories;
all 1,884 raw samples; and independently reconstructed runtime query hashes,
counts, placement/output bytes and complete timed checksums. The public wire,
exact guarded range and admitted-region checks passed for each selected target.
The new checker passed 34 cases and 279,552 guarded/oracle queries for each of
scalar, AVX2 and AVX-512 in each variant.

The source capture is `seriespack-runtime-ranges-20260910`, digest
`e7a283d72d629909b2bd5e0124e3d2eadbf092d55206f6878066672dd6bc71ab`.
Only the private callback ABI changes between variants: `operations.h`,
`native.cpp`, and the historical `physical.cpp` path. The public bound decode
method and common harness sources are identical. Source snapshots identify
variants; the wrapper rehashes both trees at completion. Library archives were
not returned for local rehashing; fresh compilation records and measured
executable identities establish the matched builds.

[Per-case results](../ikea-composition/archive/validation-20260911.md) retain all 68 runtime cases,
73 fixed-16/control cases and 16 bulk cases, including medians, raw ranges and
four block medians. Raw JSON, binaries, source archive and command logs remain
with the worker artifact named in the evidence record.
The shared helper also retains [all ordered individual CPU/real repetitions](../ikea-composition/archive/validation-20260911.md),
with iteration counts and counters, under a verified
[compact manifest](evidence/runtime-ranges-20260910/zen/provenance.json) and
[existing artifact reference](evidence/runtime-ranges-20260910/zen/artifact.json).

## Exact caller workload

Each case carries 4,096 runtime `{begin,end}` records (64 KiB), with 256 queries
per timed iteration. The callback receives the original coordinates and writes
the exact u32/u64 count into naturally aligned output; one indirect call/query
and `uint64(first)+uint64(last)` checksum work are common to both variants.
The cursor persists across timed batches and restarts per invocation. Binding,
wire construction, allocation and every-record oracle/canary validation occur
outside timing. The fixture constructs Local W7 and Striped W12 wire without
using a production encoder. Additional protected tests place exact input/output
envelopes against both guard orientations and make input pages read-only.

Origins below are relative to a selected tile base. Local streams are `(1,16)`,
`(1,17)` and a balanced mixed stream `(0,16),(1,16),(1,17),(7,2)`. Striped
streams are `(16,16)`, `(17,16)`, `(17,17)` and mixed
`(16,16),(17,16),(17,17),(63,17)`. Each pair denotes origin/count. Mixed streams
average 12.75 Local or 16.5 Striped output values/query. Every fixed or mixed
pattern is represented equally within each 256-query batch. The same shape
and regime use identical records across ABI, ISA, sink and Local placement
comparisons; no provider rounds or clamps a query.

| Shape / placement | Logical values | Encoded bytes | Plane-envelope bytes | Payload / head0 / head1 strides |
|---|---:|---:|---:|---|
| Local K7/H0 dense | 4,608 | 4,032 | 4,032 | 7 / 0 / 0 |
| Local K23/H16 heads gapped | 1,280 | 3,680 | 8,450 | 7 / 21 / 25 |
| Local K23/H16 independently gapped | 1,280 | 3,680 | 10,199 | 18 / 21 / 25 |
| Striped K12/H0 dense | 2,560 | 3,840 | 3,840 | 96 / 0 / 0 |
| Striped K28/H16 independently gapped | 1,024 | 3,584 | 4,514 | 128 / 77 / 81 |

Requested encoded size is 4 KiB before full-cell rounding. The 64 KiB query
stream and reported placement envelopes are separate working sets. Neither
this requested size nor moderate strides establish a cache residency/miss rate.

## What changes across shape, sink and ISA

The following ranges are median time reductions across each shape's three
Local or four Striped regimes, rather than an average over dissimilar cases:

| Shape / placement | AVX2 u32 | AVX2 u64 | AVX-512 u32 | AVX-512 u64 |
|---|---:|---:|---:|---:|
| Local K7/H0 dense | 20.5–37.4% | 17.7–33.4% | 22.7–41.0% | 19.1–36.3% |
| Local K23/H16 heads gapped | 13.9–28.3% | 10.3–24.9% | 13.7–27.8% | 11.5–26.3% |
| Local K23/H16 independently gapped | 14.5–27.5% | 10.9–23.9% | 13.9–27.7% | 11.4–25.6% |
| Striped K12/H0 dense | 30.5–66.1% | 25.7–57.2% | 29.6–64.2% | 25.3–59.6% |
| Striped K28/H16 independently gapped | 22.4–62.1% | 22.4–54.1% | 23.4–59.9% | 19.7–60.4% |

The weakest gain, AVX2 Local23/H16/u64 with heads gapped and `(1,17)`, is
16.204→14.532 ns/query, raw ranges [16.114,16.252] and [14.383,14.658].
Its ABBA block medians are 16.143/14.456/14.619/16.235. The strongest,
AVX2 Striped12/H0/u32 `(16,16)`, is 8.258→2.799, ranges [8.254,8.271]
and [2.791,2.806], block medians 8.268/2.797/2.799/8.255.

The 16-row origin boundary remains expensive. Candidate times, ns/query:

| Shape / ISA / sink | `(16,16)` | `(17,16)` | `(17,17)` | Mixed |
|---|---:|---:|---:|---:|
| Striped12 / AVX2 / u32 | 2.799 | 7.102 | 7.720 | 5.762 |
| Striped12 / AVX-512 / u32 | 2.971 | 7.077 | 7.804 | 5.818 |
| Striped12 / AVX2 / u64 | 3.301 | 7.279 | 8.128 | 6.093 |
| Striped12 / AVX-512 / u64 | 3.105 | 7.471 | 8.062 | 6.015 |
| Striped28 / AVX2 / u32 | 3.350 | 9.110 | 9.969 | 7.210 |
| Striped28 / AVX-512 / u32 | 3.606 | 9.012 | 9.821 | 7.219 |
| Striped28 / AVX2 / u64 | 3.981 | 9.223 | 9.949 | 7.520 |
| Striped28 / AVX-512 / u64 | 3.365 | 9.077 | 9.931 | 7.315 |

This corresponds to an unchanged, explicit source decision in `decode_boundary`:
origin 16/count16 uses a native 16-row region; origin17/count16 uses one scalar
value, an 8-row native region and seven scalar values. Count17 adds a second
scalar value before the 8-row region. The timing establishes the consequence
of those caller regimes, not a cycle attribution to individual instructions.

For headed Local23, adding payload gaps changes candidate fixed `(1,16)` and
`(1,17)` medians by less than 0.6%, with overlapping ranges. Mixed reads are
2.8–4.0% slower with payload gaps, and all four sink/ISA comparisons have
separated ranges; this effect was already present in the baseline. Thus the
stride penalty depends on the query mix. There is no dense headed Local23
control here to isolate the cost of head gaps themselves.

Sink effects also depend on ISA: AVX2 Local7 `(1,16)` costs 9.464 ns for u32
and 10.434 for u64; AVX-512 costs 9.474 and 9.498. Across the AVX-512 Local
cases, u32/u64 candidate medians differ by less than 0.9%. Narrow sinks do not
provide a universal speedup, and AVX-512 does not universally beat AVX2.

## Retained controls and interpretation

All 18 ordinary fixed-16 cases improve 14.4–73.5%, with separated ranges.
Their remaining comparison gaps persist: AVX-512 Local7 is 2.723 ns/query
against admitted 1.607, raw dense 1.618, predecessor 1.610 and Calico 1.916.
Striped5 is 6.956/5.936 for AVX2/AVX-512 against predecessor 3.295. These
controls retain their original fixed-16 contracts and query streams; they do
not become arbitrary-range controls for the new workload.

Unchanged controls show substantial context effects. Nine of 18 raw-dense
cases have separated losses; AVX2 Striped6 changes 2.219→3.424 (+54.3%),
with ABBA medians 2.202/3.344/3.466/2.236. AVX-512 Striped5 raw changes
3.014→4.162 (+38.1%). Their callback signatures did not change. The actual
linked runtime callers remove aggregate stack marshaling and drop 28→24
useful loop instructions for u32 and 27→23 for u64, while retaining runtime
endpoint loads and one indirect call. This supports an ABI mechanism, but
caller/code placement and other context changes remain part of the measured
pair. Instruction counts are not cycle estimates.

None of the 16 retained bulk/control cases has a separated loss. The largest
median loss is AVX2 Local64/u64 encode, +0.70%, with overlapping ranges.
Local23/H16/u32 decode changes +0.26% AVX2 and +0.09% AVX-512, so the earlier
separated bulk losses do not recur in this linked program. AVX-512 Local6/u8
decode improves 0.005641→0.005306 ns/value (−5.9%), against matched after
predecessor 0.004114 and Calico 0.005129. It remains 29.0% and 3.5% slower,
respectively; AVX2 is 0.010545. The previous first-scope Local6 halving is not
reproduced: this run's baseline already has the faster absolute time. Different
linked context, case order and worker prevent treating this as a controlled
relink isolation or dismissing the earlier observations.

The new evidence supports scalarizing this callback beyond aligned H0/u64
reads. It also makes unaligned boundary lowering a concrete remaining question:
runtime dispatch savings do not remove scalar edge work. No direct composition
executor, u16 sink, arbitrary legal range matrix, per-query cache miss rate or
equivalent arbitrary-range predecessor was measured here.
