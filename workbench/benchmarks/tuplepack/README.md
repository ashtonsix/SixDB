# TuplePack routine comparisons

This small suite measures the maintained module's ordinary boundaries and native
consumers. Historical layout matrices and alternative lowerings remain with the
[TuplePack spike](../../spikes/tuple-layout/viability.md); they are not public presets.

Configure with `-DSIXDB_BENCHMARKS=tuplepack`, build `ikea_tuplepack_bench`, and run
on a compatible pinned CPU with cases/repetitions sequential. From Linux:

```sh
cmake -S . -B build/benchmarks/tuplepack/avx2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_BENCHMARKS=tuplepack \
  -DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5
cmake --build build/benchmarks/tuplepack/avx2 --target ikea_tuplepack_bench -j1
taskset -c 0 build/benchmarks/tuplepack/avx2/workbench/benchmarks/tuplepack/ikea_tuplepack_bench \
  --benchmark_min_time=0.1s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=false
```

Choose an allowed CPU. ARM uses `armv8.2-a+simd`/`neoverse-v2`; the Zen AVX-512
profile uses `znver5`, including VBMI. The [run helper](run.sh) accepts
`TUPLE_PROFILES='avx2 avx512'` or `neon`, builds profiles sequentially, runs checks,
and retains binaries, compilation commands, build RSS/time and timing JSON.
It works with `worker.py` or locally with `SIXDB_RESULTS` and `SIXDB_CPU` set.
The routine set includes three shape/placement combinations (64/8 rows over one-byte tuples,
and four rows over scattered large tuples, with full/sparse masks).
Set `TUPLEPACK_BROAD=1` to register every packet shape with tight, strided,
compact and scattered placement. Use `--benchmark_filter` to select families.

| Family | What is timed | Interpretation |
| --- | --- | --- |
| `point/{1,2,4}/read/{control,body,ordinary}` | Runtime ordered maps over four codes, repeated random trace, 1,024 rows/stride 16 | Same-wire selected-byte control; body vs bound ordinary read |
| `point/{1,2,4}/write/{control,body,ordinary,control_effects}` | Two alternating valid inputs, masked width admission and spare preservation | Ordinary includes range/capacity/source journal; `control_effects` adds those obligations to the independent same-wire control |
| `wide/64/{read,write}` | Reordered 64-code native/buffered/ordinary calls over 64-byte units | `control` is the module's native body endpoint; it is a boundary comparison, not independent prior art |
| `scan/packet4/{16,64}` | Four-row native projection, sequential scans of 1,024/65,536/1,048,576 rows | Same information, different placement; no PMU/cache-tier claim from allocation size alone |
| `pipeline/decode_repack/{inline,cps}` | Same decoded 64-code packet, byte-code reduction and repack body | Two useful shared bodies; effects/publication excluded on both paths |
| `composition/128/{body,ordinary}` | Two native packets update disjoint nibble codes sharing 64 physical bytes | Ordinary includes both packets' width admission and qualified effects; body is the two underlying native writes |
| `packets/<Rows>/<layout>/<all,half>/sum` | Native read and reduction over 1,024 original rows | Shape, physical extent and stride vary independently; time is normalized by original rows, including inactive rows |
| `packets/<Rows>/<layout>/<all,half>/update/{body,ordinary,cps}` | Shared native decode/toggle/write bodies | Body excludes admission/effects; ordinary and CPS include identical width checks and qualified journals; maintenance/publication excluded |

Time is per benchmark iteration; `items_per_iteration` gives rows/operations for
normalization. `point` controls use runtime masks/shifts rather than a fully
constant typed struct, and equal stride deliberately removes packed-density
benefits. `control_effects` has the same declared source/span and capacity
obligations but a fixture-specific footprint; it does not price generic binding.
Wide controls and inline/CPS use equal logical work, but compiler fusion may
remove or reorganize operations. Inspect codegen when interpreting their ratio.

No microbenchmark here selects a layout for a mixed workload. The analyser spike
has a concrete counterexample to adding isolated operation costs. Prepared-map
working sets, contention, large sparse native writes and real compound consumers
remain reasons to extend selectable coverage when a new use warrants it.

Packet instantiations are sharded by projection size to bound compiler memory;
this does not change their execution boundaries.

The initial implementation evidence is retained under
[tuple-layout/module-evidence](../../spikes/tuple-layout/module-evidence/README.md).
It distinguishes the early boundary failures from the final measured sources.
The [packet experiment](../../spikes/tuple-layout/batching/README.md) retains
the transfer-grain investigation and comparisons with repeated point operations.

## Choose an 8-byte or 64-byte packet

The separate `ikea_tuplepack_word_bench` target compares the supported GPR
operations, repeated scalar points, same-row SIMD operations and fuller SIMD
scans. Its 40 routine cases include compact two-row reads/updates, a scattered
counterexample, two-code point calls, and scans of one-byte tuples. `TUPLEPACK_WORD_BROAD=1` enables
4,439 selectable cases over extents, strides, maps, masks and consumers.
[Packet-width comparisons](words.md) explain how to use these results.

`run-words.sh` builds this target and relevant checks, and captures build time/RSS,
code size and three sequential repetitions by default. It selects the broad
matrix for evidence collection; the executable alone uses the routine set.
Use `--benchmark_filter` for a narrower question. `TUPLE_PROFILES` selects
`avx2`, `avx512` or `neon`; `baseline` runs x86 without AVX2 validation only.
