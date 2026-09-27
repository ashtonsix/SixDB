#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
for tune in neoverse-v2 neoverse-v3; do
  build="build/ikea-neoverse-v3/explore-$tune"
  mkdir -p "$out/$tune"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=ikea-neoverse-v3 -DSIXDB_MARCH=armv8.2-a+simd \
    -DSIXDB_TUNE=neoverse-v2 -DV3_SPIKE_TUNE="$tune" > "$out/$tune/configure.log" 2>&1
  cmake --build "$build" --target ikea_v3_bench -j "$SIXDB_BUILD_JOBS" > "$out/$tune/build.log" 2>&1
  cp "$build/workbench/spikes/ikea-neoverse-v3/ikea_v3_bench" "$out/$tune/"
  cp "$build/compile_commands.json" "$out/$tune/"
  taskset -c "$SIXDB_CPU" "$out/$tune/ikea_v3_bench" --check > "$out/$tune/checks.txt"
  llvm-objdump-21 -d --demangle "$out/$tune/ikea_v3_bench" > "$out/$tune/kernels.asm"
done
lscpu -J > "$out/lscpu.json"
cat /proc/cpuinfo > "$out/cpuinfo.txt"
# Build/check both first; no competing compilation during timing. Reverse the
# compiler-profile order on the second pass to expose temporal drift.
for trial in 1 2; do
  if [[ "$trial" == 1 ]]; then tunes='neoverse-v2 neoverse-v3'; else tunes='neoverse-v3 neoverse-v2'; fi
  for tune in $tunes; do
    taskset -c "$SIXDB_CPU" "$out/$tune/ikea_v3_bench" \
      --benchmark_min_time=0.04s --benchmark_repetitions=5 \
      --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
      --benchmark_out="$out/$tune/trial-$trial.json" > "$out/$tune/trial-$trial.log" 2>&1
  done
done
