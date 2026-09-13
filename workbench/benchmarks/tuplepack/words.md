# Choosing a TuplePack packet width

An 8-byte result is useful when a small consumer wants a scalar word. A 64-byte
result can amortize work over more rows or make a difficult map cheaper with
SIMD. Packet width describes the consumer interface, independently of physical
tuple extent and stride.

Start with these choices:

- **A few rows feeding scalar logic:** try `reader<8, Rows>` / `writer<8, Rows>`.
  Consecutive physical bytes with a common bit shift permit bounded word
  transfers. Preparation selects the ordinary endpoint; inline authors share
  those transfer bodies. One-row calls receive the same improvements.
- **A scan with enough useful rows:** fill a 64-byte packet. Eight one-byte rows
  fit in a GPR, but a vector scan can handle 64 in one operation.
- **Scattered or differently shifted codes:** compare the whole consumer against
  a 64-byte native packet with the same Rows and map. A wider carrier can win
  substantially even when most of its logical slots are unused.

No public transfer-method enum is needed. Use ordinary bound calls first, and
`native_reader` / `native_writer` to inline a complete register-valued consumer.
Inlining removes a boundary; its larger body does not guarantee a faster caller
for every runtime map.

## Keep a GPR consumer when SIMD wins

Prepare the same map of at most four codes on `reader<64, 2>` and `writer<64, 2>`,
then bind their native adapters. A consumer that processes two four-byte rows
can keep its scalar input and output:

```cpp
auto word = tp::native::compact_word(read.get_unchecked(first, active));
auto next = transform(word); // caller's scalar computation
auto status = write.set(first, tp::native::expand_word(next), effects, active);
```

The helpers move values in registers. Compacting discards slots after the first
four per row; expanding zeros them. An equivalent writer therefore uses the
same short map, with no additional destinations. The caller still secures the
writer's declared byte coverage: a SIMD replacement may issue a wider preserved
window than a GPR replacement, even though both change the same logical codes.

## Comparisons and reproduction

Build `ikea_tuplepack_word_bench` with `SIXDB_BENCHMARKS=tuplepack` and the desired
ISA, as in the [suite guide](README.md). The default 40 cases cover compact and
scattered two-row projections, four-row/two-code consumers, and scans of one-byte
tuples. `TUPLEPACK_WORD_BROAD=1` enables 4,439 cases; use
`--benchmark_filter` to select a subset. `run-words.sh` captures the broad set by
default; `TUPLEPACK_WORD_BROAD=0` selects the routine set.

| Control | Work |
| --- | --- |
| `points` | Existing ordinary scalar operation repeated per row, then compacted by the caller |
| `gpr` | One ordinary 8-byte packet operation, with preparation-selected endpoint |
| `gpr_native` | The 8-byte operation composed inline through its native adapter |
| `simd` | A 64-byte native operation with the same original-row count; word consumers compact it in registers |
| `simd_word` | Two-row SIMD update through the GPR compact/transform/expand bridge, including checked mutation and effects |
| `simd_full` | Eight times as many rows per 64-byte operation; scan comparison only |

Consumers hash a logical word, sum code bytes, or read/toggle/replace selected
codes. Reads use trusted bound entries after placement admission. Updates include
width/range/capacity checks and qualified byte journals;
summary maintenance and publication are excluded. Repeated points admit each row
separately, while packet operations admit the complete window. GPR stores retain
selected-byte coverage; SIMD can reissue preserved compact windows and coalesce
more effects. The comparison deliberately includes those operation-level costs.

Each timed competitor is checked against an independent bit reference before
measurement. Runs use 1,024 original rows, sequential or permuted requests,
all/alternating masks, physical extents 1/4/8/16/64 and tight/64-byte strides.
Time is CPU nanoseconds per original row, including inactive rows. Cases and
repetitions run sequentially on a pinned CPU. Working sets are repeatedly used;
the suite does not establish a cache tier or model database workload frequency.
Some parameter combinations coincide, so a matrix median is not a workload score.

## Measured operating points

The September 12 captures use Clang 21.1.8, `-O3`, assertions enabled and no LTO:
Zen 5 with AVX2 (`x86-64-v3`) and AVX-512 (`znver5`), and Neoverse V2 with NEON
(`armv8.2-a+simd`). Each profile completed 4,439 independently checked cases with
three sequential repetitions. Tables below report median CPU ns per original row.

Two rows of four consecutive seven-bit codes, four physical bytes per tuple,
stride four, permuted requests and all rows active:

| Consumer | Profile | Repeated points | Ordinary GPR | Inline GPR | Same-row SIMD |
| --- | --- | ---: | ---: | ---: | ---: |
| Word hash | AVX2 | 1.667 | 0.783 | 0.274 | 1.450 |
| Word hash | AVX-512 | 1.559 | 0.895 | 0.275 | 0.850 |
| Word hash | NEON | 1.599 | 1.086 | 0.424 | 2.871 |
| Checked update | AVX2 | 3.223 | 1.669 | 1.311 | 4.147 |
| Checked update | AVX-512 | 3.554 | 1.781 | 1.253 | 3.380 |
| Checked update | NEON | 4.932 | 2.621 | 2.246 | 7.781 |

The ordinary packet already saves meaningful boundary and admission work. Inline
composition adds a further gain for this map without changing its logical shape.

The counterexample uses the same two-row/four-code projection, but selects bytes
0, 5, 10 and 15 from 16-byte tuples, with mixed shifts and stride 16:

| Checked update | Ordinary GPR | Inline GPR | Same-row SIMD | SIMD through GPR bridge |
| --- | ---: | ---: | ---: | ---: |
| AVX2 | 11.737 | 11.579 | 4.346 | 4.475 |
| AVX-512 | 11.150 | 11.420 | 3.438 | 4.323 |
| NEON | 18.170 | 18.057 | 9.292 | 8.191 |

The bridge retains a scalar consumer while making the operation 2.2–2.6× faster
than inline GPR. This is partly a mutation-footprint choice: GPR writes four
selected bytes per row, producing seven coalesced journal spans; SIMD preserves
and reissues both 16-byte units, producing one 32-byte span. The owner must permit
the wider writes. The table does not isolate arithmetic from effects overhead.

For scans of one-byte tuples selecting one bit per row, filling a wider packet
wins despite cheap GPR access:

| Sum consumer | Ordinary GPR, 8 rows | Inline GPR, 8 rows | SIMD, 8 rows | SIMD, 64 rows |
| --- | ---: | ---: | ---: | ---: |
| AVX2 | 0.219 | 0.148 | 0.312 | 0.031 |
| AVX-512 | 0.218 | 0.151 | 0.237 | 0.029 |
| NEON | 0.338 | 0.197 | 0.850 | 0.110 |

These observations support two packet widths and shared lowering bodies, with
automatic transfer recognition during preparation. They do not support exposing
the research prototype's individual execution methods as public policies.

## Existing point calls and remaining limits

Against the frozen pre-GPR-module [motivation captures](../../spikes/tuple-layout/batching/gpr/README.md),
the median current/prior ratios across 322 matching repeated-point cases per
consumer are:

| Profile | Word hash | Sum | Checked update |
| --- | ---: | ---: | ---: |
| AVX2 | 1.023 | 0.997 | 1.044 |
| AVX-512 | 0.999 | 0.990 | 1.027 |
| NEON | 1.026 | 1.025 | 1.056 |

AVX-512 and NEON have no matching cases above 1.4×. AVX2 has eight word-hash cases
above 1.4×, reaching 1.501×, and two sum cases at approximately 1.401×. All assemble
four separate two-code point reads, with all rows active. The selected 78-byte
point kernel is [byte-identical](evidence/words-20260912/point-control.json) in the
two binaries; both caller loops still issue four two-argument indirect calls.
Stack offsets, register allocation and instruction placement differ. The exact
cause of this cross-binary timing difference remains unresolved; these captures
do not establish an intrinsic two-code kernel regression.

One ordinary four-row GPR operation takes 0.89–0.93 ns/row for those hash cases,
versus 1.78 for current repeated points and 1.19–1.21 for prior repeated points.
Use the packet operation for that consumer. Keep the control exceptions visible;
another copy of an identical point kernel is not a justified specialization.

GPR packets retain selected-byte stores, support at most eight original rows, and
do not add automatic ISA selection. Carrier choice and the owner's allowed byte
coverage remain explicit. These consumer timings exclude suspension, publication
and CPS; the GPR tests verify CPS register handoff and early completion, without
claiming a new CPS runtime operating point.

## Evidence and compilation

Retained [AVX2](evidence/words-20260912/avx2/provenance.json),
[AVX-512](evidence/words-20260912/avx512/provenance.json) and
[NEON](evidence/words-20260912/neon/provenance.json) evidence includes all competitors
and repetitions for the routine set and AVX2 exception families, plus matching
prior point controls. Each directory's `coverage.json` summarizes the full grid;
`compilation.json` records build resources, checks and object sizes. Recovery
references preserve complete samples, binaries, source snapshots and flags. The
prior controls use the separately linked motivation artifacts. Recover selected
members with the [artifact tools](../../tools/artifacts.md).

Single-job builds of the benchmark, five TuplePack behavior targets and the word
example took 97 s (AVX2), 91 s (AVX-512) and 168 s (NEON), with peak compiler RSS
of 1.06, 1.01 and 1.17 GiB respectively. These are complete target-set builds, not
incremental edit timings. The compiled GPR TU contributes 20.5/23.3/29.3 KiB in
`llvm-size`'s text column. Four row-shape TUs bound benchmark compilation memory;
ordinary 64-byte plans acquire no unused GPR endpoint storage.

The measured snapshot precedes only documentation/formatting and consolidation
of the small ISA shuffle helpers into `detail/native/shuffle.h`. The latter keeps
the same explicitly specialized, always-inlined bodies and has separate compile
and behavior checks.
