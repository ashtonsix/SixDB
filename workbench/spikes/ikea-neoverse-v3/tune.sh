#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
for tune in neoverse-v2 neoverse-v3; do
  build="build/ikea-neoverse-v3/full-tune-$tune"
  mkdir -p "$out/$tune"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_BENCHMARKS=bec256 -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE="$tune" \
    > "$out/$tune/configure.log" 2>&1
  cmake --build "$build" --target ikea_bec256_bench ikea_bec256_check ikea_bec256_composition_check \
    -j "$SIXDB_BUILD_JOBS" > "$out/$tune/build.log" 2>&1
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_bec256_check" > "$out/$tune/checks.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_bec256_composition_check" >> "$out/$tune/checks.txt"
  cp "$build/workbench/benchmarks/bec256/ikea_bec256_bench" "$build/compile_commands.json" "$out/$tune/"
  llvm-objdump-21 -d --demangle "$out/$tune/ikea_bec256_bench" > "$out/$tune/kernels.asm"
done
for trial in 1 2; do
  tunes='neoverse-v2 neoverse-v3'; if [[ "$trial" == 2 ]]; then tunes='neoverse-v3 neoverse-v2'; fi
  for tune in $tunes; do
    taskset -c "$SIXDB_CPU" "$out/$tune/ikea_bec256_bench" \
      --benchmark_min_time=0.06s --benchmark_repetitions=7 \
      --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
      --benchmark_out="$out/$tune/trial-$trial.json" > "$out/$tune/trial-$trial.log" 2>&1
  done
done
