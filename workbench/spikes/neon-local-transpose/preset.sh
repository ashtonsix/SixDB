#!/bin/bash
# Worker job: Local versus striped for the bulk_arm stripe widths with the
# maintained SeriesPack implementation (preset.cpp). Arguments pass to the benchmark.
set -euo pipefail
build="$SIXDB_SOURCE/build/neon-local-transpose"
out="$build/workbench/spikes/neon-local-transpose"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
  -DSIXDB_TUNE="${TUNE:-neoverse-v2}" -DSIXDB_SPIKES=neon-local-transpose > "$SIXDB_RESULTS/configure.log"
cmake --build "$build" --target neon_local_transpose_preset_bench_current -j "${JOBS:-$SIXDB_BUILD_JOBS}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
taskset -c "${SIXDB_CPU:-1}" "$out/neon_local_transpose_preset_bench_current" --benchmark_repetitions=7 \
  --benchmark_enable_random_interleaving=true --benchmark_min_time=0.2s \
  --benchmark_out="$SIXDB_RESULTS/preset.json" --benchmark_out_format=json "$@" \
  > "$SIXDB_RESULTS/preset.txt"
