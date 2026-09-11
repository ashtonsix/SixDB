# SeriesPack benchmarks

The maintained suite compares ordinary boundaries, placed compound consumers and
representative inline/CPS pipelines. Select it through `SIXDB_BENCHMARKS=seriespack`, or
`python3 workbench/tools/dev.py --add-benchmark seriespack` for editor support:

```sh
cmake -S . -B build/ikea/avx2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5 -DSIXDB_BENCHMARKS=seriespack
cmake --build build/ikea/avx2 --target ikea_seriespack_bench -j2
taskset -c 0 build/ikea/avx2/workbench/benchmarks/seriespack/ikea_seriespack_bench --suite=quick \
  --benchmark_min_time=0.1s --benchmark_repetitions=5 \
  --benchmark_out=build/ikea/quick.json --benchmark_out_format=json
```

Run from the repository root on Linux; prefix with `orb -m ubuntu` on the Mac.
Choose an available CPU in your affinity set. Run cases and repetitions sequentially
without competing work on that CPU. Use separate build directories per profile.
For NEON use `armv8-a+simd` / `neoverse-v2`. The measured Zen AVX-512 profile uses
`x86-64-v4` / `zen5` and
`-DCMAKE_CXX_FLAGS='-mavx512vbmi -mavx512vbmi2 -mgfni'`.

`--suite=quick` is a small routine set: 12/64-bit ordinary operations, selected
32-row pipeline recipes and a partial substituted consumer. Broaden with
`ordinary`, `placement`, `pipelines`, `stress` or `all`. Google Benchmark's
`--benchmark_filter` overrides that suite filter; `--benchmark_list_tests=true`
lists exact names. Every maintained workload validates its result before timing.

## What the controls mean

| Prefix | Comparison | Interpretation |
| --- | --- | --- |
| `ordinary/` | `bound` vs `concrete` | Same current bodies and data: erased read/point or maintained-write call versus concrete driver; measures the call boundary and optimization visibility it permits |
| `placed/` | `sum-native` vs `sum-materialized`; `mutation-bound` vs `mutation-materialized` | Same admitted expression and logical work; materialization uses native reads and the prepared bulk writer, including summary/coverage obligations |
| `pipeline-packet/` | `cps`, `inline`, `fused` | Shared source/transform/predicate/sink bodies at 16/32/64-row grains where the native carrier fits |
| `pipeline-mutation/` | `cps`, `inline`, `fused`, `scratch-calls` | **Tiny-stage stress test**: repeated cheap predicates on 16 rows, depths three/seven; scratch calls explicitly materialize handoff |

The ordinary point control exposes the entire repeated-point loop to the compiler.
Its ratio includes optimization opportunities lost at a scalar erased call; it is
not the same comparison as the historical `point/.../prior` scalar-call control.
Use a bulk operation or a concrete binding when the loop itself is available to
compose. Report both controls when investigating a point-call regression.

Placed cases cover separated, interleaved and substituted storage, all/partial
selection, and 20/31/63-bit headed values. Inputs have 8,192 logical rows; these
are warm operation microbenchmarks. They do not measure cold DRAM scheduling,
page faults, concurrency, publication or full queries.

Pipeline recipe names encode source, map, filter and sink. Sources are Striped12
and Local23, maps are add/xor/multiply, predicates are upper-bound/range/low-bits,
and sinks are selected sum or maintained replacement. An `empty` recipe obtains
no survivors from actual data and skips its sink. It is distinct from a planner-
known constant-false predicate that inline compilation may remove before loading.
The latter failure mode remains in the campaign evidence.

Compare matching grains and also the best measured grain for each style. Selecting
from the measured set is tuning evidence, not a held-out validation. ARM admits
up to 32 rows for the tested 32-bit carrier; x86 admits 64. A tiny-stage CPS loss
does not establish the runtime cost of the intended coarser execution point.

## Broader prior-work comparison

Configure `-DIKEA_COMPARISONS=ON` and build `ikea_seriespack_compare`. This opt-in target
adds all-width decode/write cases and the specialized same-wire controls retained
in [Workbench](../../spikes/ikea-composition/ikea2-campaign/README.md).
It links current Ikea and a separately named frozen predecessor library. The comparator
headers and include paths are isolated; neither implementation can shadow the other.
For a small selection:

```sh
taskset -c 0 build/ikea/avx2/workbench/benchmarks/seriespack/ikea_seriespack_compare \
  --benchmark_filter='^(decode|overwrite)/.*k(5|12|28)(/|$)' \
  --benchmark_min_time=0.1s --benchmark_repetitions=5
```

Names `current` denote the active Ikea implementation, `predecessor` the frozen
first implementation, and `prior` the specialized narrow/wide56 wire controls. `control-coverage` adds matching issued-write
obligations to a raw encoder. `predecessor-materialized-sum-coverage` performs equivalent
replacement maintenance. Raw encode and maintained replacement are different work.
`get16` consumes every output; `get16-ends` retains the earlier endpoint-only sink.

The [shared evidence helper](../../tools/evidence.py) can summarize local
Google Benchmark JSON without a worker:

```sh
python3 workbench/tools/evidence.py summarize candidate=build/ikea/quick.json \
  --output build/ikea/quick-summary
```

For captured machine comparisons, [run.sh](run.sh) is an optional worker adapter.
It runs validation, pinned timings and code-size collection; the shared worker
handles source and host retention.
`IKEA_COMPARE=0` (the default) selects only maintained cases; `IKEA_PROFILE` selects
`avx2`, `avx512` or `neon`. `summarize.py OUT --worker JOB_DIRECTORY` exports compact
comparison tables and per-recipe grain choices. Re-run `summarize.py OUT` offline
to regenerate them from retained CSV. Evidence belongs with the investigation that interprets it. The exporter also
understands historical `ikea2`/`ikea` comparison labels without rewriting those receipts.

## Runtime versus compilation cost

`ikea_seriespack_catalog_inline` and `ikea_seriespack_catalog_cps` contain the same 36 source/map/filter/
sink recipes at 32-row grain. The inline catalog instantiates complete recipes;
CPS reuses stage functions through tables. Both are independent compiled TUs.

```sh
cmake --build build/ikea/avx2 --target ikea_seriespack_catalog_inline ikea_seriespack_catalog_cps -j1
python3 workbench/tools/compile_probe.py build/ikea/avx2 \
  --source workbench/benchmarks/seriespack/catalog_inline.cpp --source workbench/benchmarks/seriespack/catalog_cps.cpp \
  --output build/ikea/catalog-compile
python3 workbench/benchmarks/seriespack/build_evidence.py build/ikea/catalog-compile --sizes \
  build/ikea/avx2/workbench/benchmarks/seriespack/libikea_seriespack_catalog_inline.a \
  build/ikea/avx2/workbench/benchmarks/seriespack/libikea_seriespack_catalog_cps.a
```

Compare compile wall time, max single-process RSS, code sections, tables and debug
size separately. Catalog archive sizes are not whole-application savings after
link-time dead-code removal. `IKEA_PIPELINE_CATALOG=1` collects this on a worker.

For measured rebuild scope after representative edits:

```sh
python3 workbench/benchmarks/seriespack/incremental.py build/ikea/avx2 --isa avx2 \
  --output build/ikea/rebuilds --jobs 2
```

This warms maintained targets, touches one admission TU, one native-write header
and one integration example in turn, and restores their mtimes. Source bytes never
change. It records actual Ninja edges, wall/RSS and artifact hashes, including an
unchanged-build control. Do not edit those sources concurrently. It measures the
current boundaries, not a pre/post refactor speedup. `IKEA_INCREMENTAL=1` runs it
on the worker after timing has finished.

The [retired predecessor suite](../../spikes/ikea-composition/seriespack-predecessor/README.md)
preserves additional resident-footprint, dependent-point and Calico workloads.
Replacing its active entry does not establish that coverage for this suite.
