# Why NEON K=17–32 was slow

This is the original diagnostic and its captured measurements. The reduction
has since been [graduated, audited and remeasured](MASK-GRADUATION.md), with the
throughput harness corrected. Its historical paired results remain unchanged.

The width discontinuity is primarily a production kernel/code-generation defect
in the shared four-byte predicate-to-mask reduction. The earlier assembly audit
missed its scalarized narrowing. A separate throughput-trace limitation also
needs qualifying; it does not explain the discontinuity.

## Storage and affected path

The K=1–64 runtime table uses `dense_source<format<K>>`: **Local geometry, H=0**,
with eight rows per tile. Whole bytes and the remaining low-bit planes are local
to that tile. It does not use the Bulk/striped geometry or separated prefix heads.
K17 has one residual bit; K24 and K32 have none. K33 has one residual bit and K64
has none. Consequently, K32's loss cannot be attributed to residual decoding.

The decoder selects 32-bit carriers for K17–32. All five comparisons then use
`ikea::detail::filter::bits<4, Op>` in
[`compare.h`](../../../ikea/include/ikea/detail/filter/compare.h). That body narrows
four u32 predicates to u16, combines them with a zero upper half, then narrows to
bytes. Pinned Clang 21.1.8 scalarizes the second narrowing. Explicit intrinsics
did not guarantee the intended machine instructions.

## Exact original disassembly

Recovered from job `20261015T130145Z-06900f35`, the original
[`runtime-neon` archive](evidence/runtime-neon/artifact.json).
`neon-disassembly.txt` SHA256:
`b02e2940d8761881fc672f7ebf7e3974c8cf65c89626206a540a64158db332e9`.
This is the captured measurement binary, not a later local rebuild.

The timed dependent K32 equality loop includes:

```asm
103ec0: cmeq v1.4s, v1.4s, v0.4s
103ec8: xtn  v1.4h, v1.4s
103ecc: umov w11, v1.h[0]
103ed0: mov  v3.b[0], w11
103ed4: umov w11, v1.h[1]
103ed8: mov  v3.b[1], w11
103edc: umov w11, v1.h[2]
103ee0: mov  v3.b[2], w11
103ee4: umov w11, v1.h[3]
103ee8: mov  v3.b[3], w11
```

Four repetitions per sixteen-row filter produce **16 scalar extracts and 16
scalar-to-vector byte inserts**, before the weighted horizontal reductions.
The independent loop at `104000..1049a0` contains 32 vector comparisons, 128
extracts, 128 inserts and eight result-mixing multiplies. It has no calls or
stack accesses. The serial loop has four comparisons and one result-mixing
multiply. All sixteen result bits participate in the recurrence.

K16 uses the u16 reduction. K33–64 use a u64 reduction that directly extracts
the two predicate lanes; it avoids the byte reconstruction. This permits K64
to beat K32 despite loading twice as much payload.

## Counterfactual measurement

[`neon-mask.cpp`](neon-mask.cpp) retains the production decoder, comparisons and
complete result recurrence. Its research-only control replaces the four-lane
mask reduction. An all-zero/all-one u32 predicate has identical u16 halves, so
one u16-to-u8 narrowing and weights `{1,0,2,0,4,0,8,0}` suffice. The other four
bytes are ignored. This removes the zero-padding and scalar reassembly.

The native control's timed K32 loop emits:

```asm
3c474: cmeq v0.4s, v0.4s, v4.4s
3c478: xtn  v0.8b, v0.8h
3c47c: and  v0.8b, v0.8b, v10.8b
3c480: addv b0, v0.8b
3c484: fmov w11, s0
```

Both native K32 measurement functions retain ten copies of the four-vector
comparison: warm-up, serial and eight throughput streams. The current function
contains 160 `umov` instructions; the control contains zero. Native disassembly
SHA256: `79673d77cf70430f8e0a888890102b37b67722236ddab37d7b61442aa74695a3`.

Fresh Graviton4/Neoverse V2 `c8g.large`, pinned CPU, Clang 21.1.8, O3,
`armv8-a+simd`, `mtune=neoverse-v2`, loop/SLP vectorizers disabled. Seven
repetitions of 500,000 iterations; full activity. Below are medians in **ns per
16-value window**, including query loads, result/address recurrence and loop
control. Throughput uses the distinct-salt trace described below. These are
paired controls in one new binary, not replacements for the old captured rows.
Candidate order is fixed; no randomized trial or DRAM claim is made.

| Equality width | Current latency | Control latency | Current reciprocal throughput | Control reciprocal throughput |
|---:|---:|---:|---:|---:|
| 16 | 11.15 | 11.11 | 1.78 | 1.78 |
| 17 | 28.27 | 18.55 | 12.71 | 5.98 |
| 24 | 22.99 | 12.73 | 10.14 | 3.26 |
| 32 | 22.29 | 12.08 | 9.93 | 3.01 |
| 33 | 21.20 | 20.73 | 7.89 | 7.89 |
| 64 | 13.75 | 13.75 | 5.78 | 5.78 |

K16/33/64 call the unchanged implementation in both candidates; differences are
run variation. K17 still pays for residual decoding after the reduction is
repaired. The control is not an assertion that this reduction is optimal.

| K32 predicate | Current latency | Control latency | Current reciprocal throughput | Control reciprocal throughput |
|---|---:|---:|---:|---:|
| == | 22.29 | 12.08 | 9.93 | 3.01 |
| < | 22.13 | 12.17 | 9.82 | 2.98 |
| <= | 21.94 | 12.18 | 9.85 | 2.93 |
| > | 22.21 | 12.05 | 9.89 | 2.99 |
| >= | 22.18 | 12.17 | 9.85 | 2.98 |

The independent scalar oracle passes K17–32, all five predicates, all 64 windows,
six boundary queries and five activity masks, including empty and sparse masks.
Assertions remain enabled in this release build. Local Linux/NEON checks and
the native worker both passed. All 560 measurement rows, oracle output and
host/source receipts are retained in [`evidence/neon-mask`](evidence/neon-mask/).
The archive retains the binary, full disassembly and captured source.

An auxiliary LLVM-MCA 21 V2 model of the isolated four-lane `current32` and
`vector32` witnesses reports 27 versus 10 micro-ops and block reciprocal
throughput 5.0 versus 1.7 cycles. This includes constant materialization/load,
excludes return, and is a static register-input model, not measured consumer
latency. Native paired timings above establish the practical effect.

## Throughput trace limitation

The original monomorphic harness keeps eight separate recurrence registers but
uses the same iteration salt for all streams. The independent scalar replay in
[`neon-trace.py`](neon-trace.py) shows that, for all six equality fixtures above,
the streams converge after five warm-up steps. All 100,000 timed iterations
then issue eight filters against one shared window. Empty inline assembly
prevents common-subexpression elimination; the disassembly confirms eight
executed filters. Thus this measures independent computation with correlated
addresses, not eight distinct data streams. The issue is in the shared harness,
not specific to NEON.

The control trace uses `i + j*0x9e37` for stream j. It averages 7.375 distinct
windows per iteration for these equality fixtures, while preserving each
stream's result dependency. It also adds salt arithmetic and changes scheduling,
so its difference cannot be attributed exclusively to cache locality.

| Equality width | Current, original trace | Current, distinct trace | Control, original trace | Control, distinct trace |
|---:|---:|---:|---:|---:|
| 17 | 12.74 | 12.71 | 5.94 | 5.98 |
| 24 | 10.11 | 10.14 | 3.16 | 3.26 |
| 32 | 9.93 | 9.93 | 2.75 | 3.01 |

All entries are reciprocal-throughput ns/window. The mask-reduction loss remains
under both traces. The old equality throughput numbers need the address-sharing
qualification; they should not be relabeled as distinct-window throughput.
The new trace is still a warmed finite workload, not uniform random or cold
memory traffic. Serial results remain complete-consumer recurrence latency,
not a standalone instruction latency.

## Consequence

Repair the common NEON u32 predicate-to-mask reduction before judging the format
or adopting a different decoder for K17–32. Reuse this diagnosis for the shared
TuplePack comparator, but do not claim TuplePack performance from these SeriesPack
measurements. Future throughput qualification should distinguish correlated and
distinct streams. This investigation adds a diagnostic control and evidence;
production kernels and defaults were unchanged at this diagnostic capture.

For the historical paired experiment, recover the source captured with
[`evidence/neon-mask`](evidence/neon-mask/artifact.json), then run `worker.py run
workbench/spikes/filter-composition/neon-mask.sh --machine neoverse-v2
--instance-type c8g.large --capacity on-demand --env SIXDB_BUILD_JOBS=2`.
The current checkout uses the later full-word reduction and retains the initial
byte-narrowing control, so running it now is not the original broken-versus-fixed
comparison. The graduation report identifies the final paired measurements.
Run `python3 workbench/spikes/filter-composition/neon-trace.py` for logical
address counts. Prefix Linux commands with `orb -m ubuntu` from macOS.
