# GNR runtime ranges across the scalar callback ABI

All **68 GNR runtime cases improve**, by **5.86–61.13%** median CPU time.
Every before/candidate raw range is separated and both ABBA edges agree. This
matches [Zen's direction](runtime.md), while
[V2 retains twelve separated runtime losses](neoverse-v2.md).
The universal ABI improvement does not make every GNR boundary, sink or ISA
choice equally fast: AVX-512/u64 native16 regions remain a conspicuous cost.

## Provenance and ordered evidence

Job `20260910T205354Z-b34bc457` used an existing c8i.large (Intel Xeon
6975P-C), CPU 0, Clang 21.1.8, Granite Rapids tuning and x86-64-v4 plus
VBMI/VBMI2/GFNI. Both AVX2-labelled and AVX-512 endpoints share those enabled
features; this is not an AVX2-only build. The source is isolated capture
`seriespack-runtime-ranges-isolated-20260910`, digest
`2feb899a298657d5b59aa580dfedf371e61ee21200b6f53442198fdacf713cba`,
identical to V2 and differing from the Zen capture only in the runner's work
directory. All benchmark/oracle/query sources and the original three-file
callback candidate are unchanged.

The [independent audit](evidence/runtime-ranges-20260910/gnr/audit.json)
verifies 490 source files against capture, manifest and archive, the exact
`operations.h`/`native.cpp`/historical `physical.cpp` candidate, 35 fresh objects
per variant, every check/command and both measured executable hashes. Runtime
guards passed 34 cases and 279,552 guarded/oracle reads for each of scalar,
AVX2 and AVX-512 per variant, alongside the existing public wire, guarded range
and admitted checks. All 1,884 raw samples, exact 68 runtime/73 fixed-16 and
control/16 bulk inventories, and every runtime query hash, byte counter and
timed checksum validate.

[All ordered CPU/real repetitions](../ikea-composition/archive/validation-20260911.md)
are retained through shared `evidence.summarize`, including iteration counts
and requested counters. The [manifest](evidence/runtime-ranges-20260910/gnr/provenance.json)
records input hashes and execution order; the [artifact reference](evidence/runtime-ranges-20260910/gnr/artifact.json)
recovers the original sources, logs, binaries and JSON. The
[comparison table](evidence/runtime-ranges-20260910/gnr/comparisons.csv)
keeps all medians, raw extrema and four block medians. Each ABBA block has three
repetitions per case, 0.03 s minimum. Times below are CPU ns/query (iteration
time divided by 256); bulk uses 8,192 values/iteration. Raw ranges are extrema,
not confidence intervals. The exact [runtime records and physical placements](runtime.md#exact-caller-workload)
match the other architectures: real typed output, one indirect call, shared
endpoint checksum, independent wire construction, and no rounded queries.

## ABI gains across the complete slice

Ranges of median time reduction across each shape's three Local or four
Striped regimes:

| Shape / placement | AVX2 u32 | AVX2 u64 | AVX-512 u32 | AVX-512 u64 |
|---|---:|---:|---:|---:|
| Local K7/H0 dense | 16.0–31.8% | 15.4–33.0% | 15.3–31.1% | 16.1–37.8% |
| Local K23/H16 heads gapped | 8.9–22.8% | 6.1–17.6% | 8.3–22.5% | 12.6–27.3% |
| Local K23/H16 independently gapped | 8.6–20.0% | 5.9–15.9% | 9.1–19.9% | 13.2–27.4% |
| Striped K12/H0 dense | 17.4–61.1% | 20.4–51.6% | 16.3–60.2% | 20.5–48.6% |
| Striped K28/H16 independently gapped | 8.5–53.0% | 9.6–36.0% | 10.4–52.3% | 11.3–46.8% |

The strongest case, AVX2 Striped12/u32 `(16,16)`, changes 10.117→3.933,
with ranges [10.093,10.145] and [3.919,3.942], and ABBA medians
10.121/3.935/3.921/10.112. The weakest, AVX2 Local23/u64 independently
gapped `(1,17)`, changes 19.417→18.278, ranges [19.368,19.460] and
[18.253,18.342], block medians 19.438/18.285/18.272/19.391.

## Boundary, sink and ISA penalties remain

Candidate times in ns/query; origin/count coordinates refer to the same
runtime query streams used in the other reports:

| Shape / ISA / sink | `(16,16)` | `(17,16)` | `(17,17)` | Mixed |
|---|---:|---:|---:|---:|
| Striped12 / AVX2 / u32 | 3.933 | 9.357 | 10.322 | 7.447 |
| Striped12 / AVX-512 / u32 | 4.028 | 9.476 | 10.448 | 7.444 |
| Striped12 / AVX2 / u64 | 4.588 | 9.784 | 10.830 | 8.101 |
| Striped12 / AVX-512 / u64 | 7.852 | 9.430 | 10.393 | 7.868 |
| Striped28 / AVX2 / u32 | 4.748 | 12.379 | 13.565 | 9.664 |
| Striped28 / AVX-512 / u32 | 4.796 | 12.574 | 13.284 | 9.623 |
| Striped28 / AVX2 / u64 | 6.413 | 13.009 | 14.208 | 10.713 |
| Striped28 / AVX-512 / u64 | 8.159 | 12.411 | 13.192 | 10.230 |

At Striped12 `(16,16)`, candidate AVX-512/u64 is **71.1% slower** than
AVX2/u64, and almost twice AVX-512/u32. At headed Striped28 the corresponding
ISA penalty is **27.2%**. Those differences already exist in the baseline.
Shifted origins narrow or reverse the ISA comparison: Striped12/u64 `(17,16)`
is 9.430 AVX-512 versus 9.784 AVX2. An ABI improvement therefore does not settle
the native region's carrier/ISA choice. All these comparisons retain full raw
ranges in the evidence; the table does not hide them behind an aggregate score.

The unchanged boundary policy still uses native16 at origin16, and scalar
edges plus native8 at origin17. Scalarizing the callback does not remove those
edges. The `(16,16)` AVX-512/u64 exception also means that boundary work alone
does not explain every slow region. No per-instruction cycle attribution or
counter-based diagnosis was made here.

Adding payload gaps to Local23's already gapped heads increases candidate
mixed time by 4.62% for AVX2/u32, 4.83% for AVX2/u64 and 4.52% for
AVX-512/u32, with separated ranges. AVX-512/u64 changes only +0.24%, with
overlap. Fixed `(1,16)` and `(1,17)` placement differences stay below 0.30%
and all overlap. Those specific mixed penalties align more closely with Zen
than with V2, where independently gapped mixed reads are faster. Head-gap cost
itself is not isolated because this slice has no dense headed Local23 case.

## Retained controls and inference limits

All 18 ordinary fixed-16 cases improve 20.6–64.2%, with separated ranges.
Meaningful comparison gaps remain: AVX-512 Local7 changes to 7.304 ns/query,
against admitted 6.228, raw dense 6.237, predecessor 6.523 and Calico 2.922.
Striped5 changes to 8.311, versus predecessor 6.922. These controls preserve
their fixed-16 facts; they are not arbitrary-range substitutes.

Four unchanged resident controls have separated losses: Calico Local56
5.977→6.003 (+0.44%), Calico Striped5 11.375→11.832 (+4.02%), AVX2
Local56 admitted 2.482→2.503 (+0.86%), and AVX2 Striped5 raw dense
5.564→5.652 (+1.57%). Their signatures did not change, so linked context
remains part of the pair. Other median changes and overlapping ranges are
retained without a sign-based filter.

No retained bulk case has a separated loss. Separated improvements are
Local6/u8 decode, AVX2 0.011924→0.011466 ns/value (−3.85%) and AVX-512
0.009174→0.008811 (−3.96%); AVX2 Local64/u64 decode 0.162600→0.159970
(−1.62%); and unchanged predecessor Local6 decode 0.009795→0.009714
(−0.83%). The largest median loss, unchanged predecessor Local6 encode
+0.87%, has overlapping ranges. Headed Local23 decode/encode changes remain
in the complete bulk table rather than being discarded as controls.

The measured linked runtime callers retain dynamic begin/end loads and one
indirect call, and their useful loop counts fall 28→24 instructions for u32
and 27→23 for u64. This supports the x86 ABI mechanism without assigning every
timing change to those four instructions. This run supports the scalar callback
for the measured GNR regimes, while leaving the boundary/sink/ISA costs above
and the V2 losses as explicit follow-up questions. No cold-memory, u16 or
equivalent arbitrary-range predecessor claim follows.
