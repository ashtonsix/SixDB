# Width-56 body experiment

2026-09-10. A bounded implementation of the [wider-body proposal](../wider-bodies.md):
exact seven-byte little-endian bodies, no head and no residual tail. This is a
body experiment, not an implementation of all widths 1–64 or a selected general
fragment protocol. The three-target measurements below show better large random
reads than the prior and the tighter fixed-plane control, with a remaining
Zen encoding tradeoff against that control.

[Back to the integer spike](../README.md). Read by question:

| Question | Section |
| --- | --- |
| What improved, and what still loses? | [Hardware findings](#hardware-findings-2026-09-10) |
| What crosses the native seam, and which bytes are accessed? | [Data and native work](#data-and-native-work) |
| Is the comparison matched and the plane control competitive? | [Measurement contract](#controls-and-measurement-contract), [control repair](#why-the-first-control-was-tightened) |
| What does a larger compute region buy? | [Optional 32-value encoder](#a-larger-encoding-region-over-unchanged-packets) |
| How do I check or reproduce it? | [Validation and commands](#validation-and-reproduction); [all retained captures](../evidence/README.md) |

## Data and native work

Eight values occupy 56 bytes. Repeating those packets is a contiguous AoS array;
the benchmark's 256-value endpoint occupies 1,792 bytes. No header or padding is
stored. The [native fragments](kernels.h) carry eight u64 lanes on AVX-512 BW+VBMI,
four on AVX2, and two on NEON. The [outer endpoints](api.h) provide point, aligned
get16, encode/decode of 256 values and a modular-u64 sum of 256 values.

The sum consumes the native fragment directly and retains only native partial
accumulators. The inspected code has no intermediate value-array stores or
stack spills. The public get16 result is deliberately materialized, while sum
does not visit that endpoint. This tests matching native grains; it does not
solve mismatched-grain or multi-source composition.

NEON encoding groups four native pairs into an inline packet region for exact
16+16+16+8-byte stores. Its decoder and sum need no eight-value assembled carrier.
That is a concrete case where the useful encoding region differs from the
decoding/consumption grain. AVX2 uses overlapping stores that agree in the overlap;
their issued size is 64 bytes per packet but their union is exactly 56 bytes.
Native decode fragments borrow an admitted whole packet and identify their part
at compile time. A NEON fragment can read neighbouring encoded bytes inside that
packet; it is not a general two-value span reader with only fourteen readable
bytes. Arbitrary final partial packets are outside these fixed-extent endpoints.

The point kernel uses two four-byte loads at relative offsets 0 and 3: seven
required bytes, eight issued load bytes, and a seven-byte union. It never reads
the following value. All eight-value packets fit the actual load/store extents.

| Target | Get16 packed accesses relative to its first value | Union |
| --- | --- | --- |
| AVX-512 BW+VBMI | Two mask-zeroed 56-byte loads at 0 and 56 | `[0,112)` |
| AVX2 | 16-byte loads at 0/12/28/40 and 56/68/84/96 | `[0,112)` |
| NEON | 16-byte loads at 0/14/28/40 and 56/70/84/96 | `[0,112)` |

In the inspected target code, Clang fully unrolls the fixed-256 AVX-512 endpoints
(decode/sum 751 bytes each, encode 559 bytes). AVX2 and NEON retain loops. This is
an observed compiler choice, not a claim that full unrolling is the best preset.

## Controls and measurement contract

All four arms have the same logical values and opaque outer calls, separate TUs,
no LTO, and the same disjoint-input/output contract:

| Arm | Stored representation | Implementation |
| --- | --- | --- |
| `local_aos7` | Seven adjacent bytes per value | Fresh native body fragments |
| `prior_shape` | Calico's three byte planes followed by one u32 plane | Pinned Calico `planes::Codec`, supplied a constant width-56 `Shape` |
| `fixed_planes` | Exact same bytes as `prior_shape` | Fresh native get16 fragments; width-specific bulk loops, no Shape walk or repeated output reconstruction |
| `plain_u64` | Eight adjacent bytes per value | Plain copy, point and sum controls |

The fixed-plane control distinguishes layout effects from the prior API's
overhead, although compiler lowering and instruction choices remain different.
The prior's sum uses its reconstructed u64 output; local sum directly consumes
native fragments. They produce the same modular sum but are intentionally
different execution regions. The prior input is the parent's existing pinned
[shared dependency](../prior-source.json), not another copy of Calico sources.

The default bulk comparison has 32,768 values: 224 KiB packed input plus 256 KiB
u64 output for encode/decode, or 512 KiB total for the plain-copy arm. A complete
warm pass precedes five sequential repetitions. The plain-copy control calibrates
one shared pass count to at least 20 ms; every arm then performs that same work.
Sum has only its input array. These are resident controls whose working sets fit
the tested private L2 capacities; capacity alone is not a cache-residence proof.

Capacity cases use `2^10`, `2^16`, and `2^26` logical values, giving 7 KiB,
448 KiB and 448 MiB packed arrays, versus 8 KiB, 512 KiB and 512 MiB plain arrays.
Each case holds one 64-byte-aligned payload allocation and prefaults all bytes.
There is no stored address trace. Dependent point reads incorporate the full
decoded value in a 64-bit address state; independent point/get16 issue groups of
eight independent requests. get16 starts at a multiple of sixteen.

Each of five repetitions uses 1,048,576 requests and a separate deterministic
seed, identical across arms. After timing, an independent logical generator
checks every returned value and the exact result/address trace. A separate
post-timing census records distinct required payload lines and line visits.
It neither runs during timing nor warms the same trace before measurement.
Repeated traces can still revisit cache lines. Every row reports residence as
unestablished; large allocation does not certify DRAM service for each request.

Required lines exclude computed constant tables, dispatch descriptors and
materialized get16 outputs. `hot_metadata_bytes` accounts only for the 40-byte
function-pointer descriptor, not all constant/compiler state. Local shuffle
indices and any surviving prior Shape accesses must be considered separately
when inspecting whole-operation traffic. PMU cycles, instructions and generic
cache misses retain raw enabled/running times; a zero generic event is not
evidence of no DRAM traffic. Page faults are recorded for each timed repetition.
Input/output/allocation byte columns count explicit harness heap buffers. They
exclude the reused 128-byte get16 output scratch and any codec-local temporary;
in particular, the prior sum's source declares a 2,048-byte reconstructed array.
Target assembly determines which temporary/Shape accesses survive compilation.
In the retained prior, get16 zeroes its output and makes four reconstruction
passes; encode traverses the input once per plane. Prior sum retains 384 bytes
of decoded stack chunks on Zen and the full 2,048-byte array on Granite Rapids
and V2. The fixed-plane bulk loops are vectorized on all three targets and have
no Shape walk, intermediate output updates, calls or spills. V2's widening tree
is still long. These are concrete implementation differences in the controls.

All 256 logical positions and all aligned groups in the 64-byte-aligned benchmark
endpoint give these exact geometric means (independent of actual trace frequencies):

| Arm | Point mean/max payload lines | Get16 mean/max payload lines |
| --- | --- | --- |
| AoS7 | 1.09375 / 2 adjacent | 2.5 / 3 adjacent |
| Prior/fixed planes | 4 / 4 | 4 / 4 |
| Plain u64 | 1 / 1 | 2 / 2 adjacent |

AoS7 meets point locality. Width 56's dense aligned get16 bound is physically
impossible at every reachable phase, as established in the parent's
[geometry proof](../locality/README.md#why-some-wider-dense-groups-cannot-be-repaired-by-permutation).
It is not claimed to satisfy that bound through padding or an excluded head.

## Hardware findings, 2026-09-10

The final full campaigns use the native fixed-plane get16 control. At `2^26`
values (448 MiB for both packed layouts), medians in ns/request are:

| Target | Dependent point: AoS7 / fixed planes | Independent point: AoS7 / fixed planes | Get16: AoS7 / fixed planes |
| --- | ---: | ---: | ---: |
| Zen 5 | 156.94 / 181.15 | 12.63 / 18.09 | 44.34 / 63.55 |
| Granite Rapids | 164.50 / 207.97 | 15.79 / 23.99 | 46.62 / 66.26 |
| Neoverse V2 | 139.02 / 196.30 | 19.99 / 32.55 | 52.06 / 136.74 |

The local layout improves dependent latency by 1.15–1.41×, independent point
throughput by 1.43–1.63× and get16 by 1.42–2.63× against this control. The original
prior's independent point costs 18.22/25.36/32.91 ns on the same three targets;
its get16 costs 187.69/211.20/316.89 ns. The latter larger advantage includes the
prior API's reconstruction cost and must not be presented as a locality-only
gain. Even the native fixed-plane comparison includes different instruction
sequences as well as different placement.

The raw-u64 control is still faster for independent point reads: 9.41/13.64/15.96
ns. AoS7 stores 12.5% fewer bytes, but does not erase every cost of packing. For
the small 1,024-value case, AoS7 and fixed planes have similar dependent-point
latency on Zen; Granite Rapids favours fixed planes by about 4%. The large-read
advantage is not a universal claim about every access regime.

The 32,768-value resident bulk comparison gives ns/256 values, using the default
packet encoder and direct native sum:

| Target | Decode: AoS7 / prior | Encode: AoS7 / prior | Sum: AoS7 / prior |
| --- | ---: | ---: | ---: |
| Zen 5 | 24.43 / 47.92 | 24.40 / 30.42 | 10.59 / 31.73 |
| Granite Rapids | 42.17 / 103.01 | 48.15 / 79.58 | 17.64 / 108.61 |
| Neoverse V2 | 59.70 / 357.04 | 71.54 / 161.35 | 46.76 / 365.85 |

Width 56 meets the prior's bulk throughput in these measured contexts. A tighter
control still matters: Zen's fixed-plane encoder takes 19.11 ns, so the default
local encoder is about 28% slower than that alternative despite beating the
prior API. The fixed-plane decode/sum controls and every repetition are in the
tables below. All timed rows had zero major and minor faults. The generic cache
miss event was zero throughout Granite Rapids and is treated as uninformative.

Complete final repetitions, checks, hardware context and source/assembly bundles:
[Zen 5](evidence/native-planes-zen5/summary.md),
[Granite Rapids](evidence/native-planes-granite-rapids/summary.md),
[Neoverse V2](evidence/native-planes-neoverse-v2/summary.md).

### A larger encoding region over unchanged packets

The optional [32-value encoder region](region_encode.h) combines four native
eight-value inputs. Three two-source byte permutations and one final permutation
produce exactly 224 bytes through three 64-byte stores and one 32-byte store.
The wire, decoder, point read, and native fragment definition stay the same.
It is selected with `--encode-region32`, separately from the default packet
encoder, on the two AVX-512 BW+VBMI targets.

Sequential default/region captures on the same worker give 24.40→22.92 ns/256
values on Zen and 48.15→43.62 on Granite Rapids (about 6% and 9% less time).
Cycles also decrease, from 108.95→102.16 and 184.71→167.07 respectively. Each
capture has its own repeated controls; samples are not pooled. The encoded bytes
and all correctness checks agree. Zen's fixed-plane encode control remains
faster at 18.76 ns in the region capture.

The inspected region endpoint is 726 bytes versus 559 for the default encoder,
with four 64-byte index tables instead of one. Its 32 input ZMM loads cover
exactly `[0,2048)`; its 24 ZMM and eight YMM output stores cover `[0,1792)` without
gaps or overlaps. No masked stores, stack accesses or spills survive. Fewer
awkward packet stores pay here despite extra code and two-source permutations;
the experiment does not isolate the contribution of every instruction change.

This is a useful concrete composition result: choose the encoding region
independently of the storage packet and the decoding/consumption grain. It does
not measure a general preset compiler, continuation cut, or mismatched-grain
protocol. Region captures: [Zen 5](evidence/encode32-zen5/summary.md),
[Granite Rapids](evidence/encode32-granite-rapids/summary.md).

### Why the first control was tightened

The first fixed-plane get16 was a width-specific scalar loop. Clang generated
64 scalar payload loads and sixteen scalar stores on x86, even with matched
disjoint-buffer contracts. Replacing that control with explicit native
fragments reduces the x86 body from 178 to 23 instructions: eight exact payload
loads and two ZMM stores. NEON uses native pairs with exact two-byte high-plane
loads and eight-byte low-plane loads. No Shape traversal, output reloads,
alias-version checks or spills remain in those native get16 bodies.

The original runs are retained to make the control change visible:
[Zen 5](evidence/auto-planes-zen5/summary.md),
[Granite Rapids](evidence/auto-planes-granite-rapids/summary.md),
[Neoverse V2](evidence/auto-planes-neoverse-v2/summary.md).
Their 3–4× group-read advantage against the weak loop is not the final finding.
The original prior and fixed-plane bulk source bodies were unchanged by this
repair. Zen prior-sum nevertheless changed from 51.72 to 31.73 ns: inspection
found the same 704 instructions, constants, jump-table destinations and decoded
stack chunks after relocation normalization. Code placement or another run
context effect remains unresolved. The subsequent default and region captures
agree at 31.73/31.80 ns; this variation is not an optimization result and is not
averaged into the final comparison.

## Validation and reproduction

[check.cpp](check.cpp) covers 706 block cases, 180,736 point reads, all packet
basis bits, byte offsets 0–63, exact fragment stores and protected boundaries.
The benchmark additionally checks 14,336 single-bit tiles and 32 random tiles
across all four arms, compares every point/get16/full decode and sum, and checks
the plane control's encoded bytes against Calico. Local ASan/UBSan and AVX2/QEMU
checks pass; emulator/development-host execution does not establish target speed.
Retained checks: [native sanitizer](evidence/native-sanitize/checks.txt) and
[AVX2/QEMU](evidence/avx2-qemu/checks.txt). Both AVX-512 encoder alternatives also
pass the full kernel and comparator checks on actual Zen and Granite Rapids.

From Linux (prefix `orb -m ubuntu` in the macOS workspace):

```sh
python3 workbench/spikes/ikea-integers/wide56/run.py --sanitize --check-only
python3 workbench/spikes/ikea-integers/wide56/run.py --target avx2-qemu --check-only
python3 workbench/spikes/ikea-integers/wide56/run.py -- --quick
python3 workbench/spikes/ikea-integers/wide56/run.py --target zen5 --encode-region32 -- --suite bulk --pmu
python3 workbench/tools/worker.py run workbench/spikes/ikea-integers/wide56/cloud.sh --machine zen5 --capacity on-demand -- zen5 --pmu
```

The short worker recipes have distinct argument conventions:

| Script | Arguments after the worker's `--` | Work |
| --- | --- | --- |
| [cloud.sh](cloud.sh) | Target, then benchmark arguments | Default encoder campaign; arguments follow the runner's `--` |
| [cloud-regions.sh](cloud-regions.sh) | Target | Default full campaign, then the optional encode32 bulk campaign on Zen/GNR, sequentially |

Use `run.py` directly to choose sanitizer checks or `--encode-region32`; those
are runner flags, not arguments to the benchmark. The [narrow run guide](../bench.md#running)
covers the parent runner and its separate campaign recipes.

The runner captures sources, reuses independent build objects and the prior
dependency, checks first, pins the timed CPU, and records target assembly.
[report.py](report.py) validates work counts, allocations, repetition completeness
and cross-arm checksums/traces before producing per-run medians and ranges.
It also reads retained compact exports. Different captures are never pooled.
