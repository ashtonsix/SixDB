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

| Family | What is timed | Interpretation |
| --- | --- | --- |
| `point/{1,2,4}/read/{control,body,ordinary}` | Runtime ordered maps over four codes, repeated random trace, 1,024 rows/stride 16 | Same-wire selected-byte control; body vs bound ordinary read |
| `point/{1,2,4}/write/{control,body,ordinary,control_effects}` | Two alternating valid inputs, masked width admission and spare preservation | Ordinary includes range/capacity/source journal; `control_effects` adds those obligations to the independent same-wire control |
| `wide/64/{read,write}` | Reordered 64-code native/buffered/ordinary calls over 64-byte units | `control` is the module's native body endpoint; it is a boundary comparison, not independent prior art |
| `scan/packet4/{16,64}` | Four-row native projection, sequential scans of 1,024/65,536/1,048,576 rows | Same information, different placement; no PMU/cache-tier claim from allocation size alone |
| `pipeline/decode_repack/{inline,cps}` | Same decoded 64-code packet, byte-code reduction and repack body | Two useful shared bodies; effects/publication excluded on both paths |
| `composition/128/{body,ordinary}` | Two native packets update disjoint nibble codes sharing 64 physical bytes | Ordinary includes both packets' width admission and qualified effects; body is the two underlying native writes |

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

The initial implementation evidence is retained under
[tuple-layout/module-evidence](../../spikes/tuple-layout/module-evidence/README.md).
It distinguishes the early boundary failures from the final measured sources.
