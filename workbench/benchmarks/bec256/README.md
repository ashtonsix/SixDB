# Bec256 routine comparisons

This suite measures the maintained headless codec without the larger-bitset
study's directories, external datasets or alternative encoders. Select
`-DSIXDB_BENCHMARKS=bec256` and build `ikea_bec256_bench`. For example, from Linux:

```sh
cmake -S . -B build/benchmarks/bec256/neon -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_BENCHMARKS=bec256 \
  -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE=neoverse-v2
cmake --build build/benchmarks/bec256/neon --target ikea_bec256_bench -j1
taskset -c 0 build/benchmarks/bec256/neon/workbench/benchmarks/bec256/ikea_bec256_bench \
  --benchmark_min_time=0.03s --benchmark_repetitions=3 \
  --benchmark_enable_random_interleaving=false
```

Choose an allowed CPU and the [pinned toolchain](../../../BUILDING.md). The
[run helper](run.sh) takes `avx512` (Zen 5), `granite`, `neon` or `avx2` as its
first argument, followed by Google Benchmark options. AVX2 runs ordinary scalar
fallback cases; it does not imply a native Bec256 profile. With `SIXDB_RESULTS`
and `SIXDB_CPU` set, the helper checks wire/replacement, pair composition and
supported headers, then retains timings, binaries, build time/RSS and symbols.
It also runs through [worker.py](../../tools/workers.md).

The native routine set has 78 cases: random bits, clustered full-byte runs and
alternating empty/full blocks. `BEC_BROAD=1` adds 15 fixed populations from 1
through 255, including complements, for 468 selectable cases. Ordinary fallback
builds register 21 / 126 cases. Use `--benchmark_filter` to narrow a question.
Every fixture rotates over 256 initialized blocks; this is resident repeated
work, with no cold-cache or database-throughput claim.

| Family | Boundary being measured |
| --- | --- |
| `codec/*/read_{exact,padded}` | Ordinary decoded 32-byte output; admitted source grants exact body bytes or 64 initialized readable bytes |
| `codec/*/encode_checked_exact` | Population, extent, alias and effect checks plus exact body stores |
| `codec/*/prepare_write` | Prepare retained private bytes, then checked exact write; useful when an owner must wait between preparation and publication |
| `codec/*/native_encode_{checked_exact,admitted_exact,wide}` | Shared body with checked exact stores, caller-admitted exact stores/effects, or a weaker trusted 64-byte writable grant without effects |
| `codec/*/{estimate_bytes,enum_bits,encode_gate144}` | Statistical estimate, exact byte-rank cost, or checked heuristic encode/decline; gate counters report declines |
| `intersection_count/*/{exact,padded}/{inline,compiled,cps,materialized}/{1,16}` | Independently addressed pairs: decode, intersect two plain query blocks, count; one or sixteen pairs per timed traversal |

Times are per iteration. `blocks_per_iteration` and `items_per_second` normalize
single blocks versus pair traversals. Pair methods use identical read grants and
shared native bodies; `materialized` calls ordinary `decode_pair`. Sources are
admitted and CPS tables prepared before timing. Sixteen pairs still
mean sixteen pipeline entries, not an artificially fused large stage.

Writes escape their output and journal addresses before timing, then use a
memory barrier each iteration. This keeps byte stores observable instead of
accidentally benchmarking only a returned length. Native admitted/wide controls
have weaker obligations than checked calls; their ratio is a boundary cost,
not independent prior-art parity. Pair adapters check their results against
bytewise intersection outside timing.

Cutoff 144 is an experimental comparison, **not a default or a proof of
incompressibility**. Decline timings exclude encoding an alternative format.
Prediction accuracy, independent same-wire predecessors, pair mutations and the
population/length directory matrix remain in the
[composition study](../../spikes/bec256-composition/README.md), with recoverable
source and evidence. The suite introduces no directory or representation policy.

The [Zen](evidence/final-zen5/cases.csv) and
[V2](evidence/final-neoverse-v2/cases.csv) comparisons retain five repetitions and
source capture `cc76b4ba960b2c801ef4c0d0a41abae442753ced0b3e9aae48c3d6195ea8bff4`.
Authored bodies are explicitly inlined in the inline/compiled controls as well
as within continuation stages. For random exact-input pairs,
inline/compiled/CPS/materialized count costs were 53.9/57.3/59.5/59.2 ns on Zen
and 163.2/162.9/163.2/160.6 ns on V2. Sixteen-pair traversals retain similar
ratios. This makes CPS credible for this simple caller, but does not erase the
larger penalty with retained directory state measured in the composition study.
The [heavier mutation consumer](../../spikes/bec256-composition/metadata-findings.md#readfilterre-encode-as-a-mutation-consumer)
compares inline, compiled and CPS bodies with exact output and effects.

Building the routine executable and both checks took 21.45 s with 217 MiB peak
compiler RSS on Zen, and 30.26 s with 224 MiB on V2, using one build job and a
prepared worker in the initial [Zen](evidence/zen5/artifact.json) /
[V2](evidence/neoverse-v2/artifact.json) build. The Bec256 library's compiled text was about 23 KiB / 21 KiB;
the benchmark executable, including Google Benchmark, about 393 KiB / 370 KiB.
These are build and artifact observations, not isolated inline-versus-CPS
compilation savings. Exact compiler commands and symbols are in each bundle.
The same snapshot also passed the [ordinary AVX2-fallback checks](evidence/checks/avx2.txt)
and its 21-case smoke run; native/CPS sections are explicitly unavailable there.
