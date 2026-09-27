#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
for variant in stock vector; do
  build="build/ikea-neoverse-v3/series-$variant"
  mkdir -p "$out/$variant"
  vector=OFF; if [[ "$variant" == vector ]]; then vector=ON; fi
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=ikea-neoverse-v3 -DSIXDB_MARCH=armv8.2-a+simd \
    -DSIXDB_TUNE=neoverse-v2 -DV3_SERIES_VECTOR="$vector" > "$out/$variant/configure.log" 2>&1
  cmake --build "$build" --target ikea_v3_series ikea_seriespack_check -j "$SIXDB_BUILD_JOBS" \
    > "$out/$variant/build.log" 2>&1
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_seriespack_check" > "$out/$variant/checks.txt"
  cp "$build/workbench/spikes/ikea-neoverse-v3/ikea_v3_series" "$build/compile_commands.json" "$out/$variant/"
  llvm-objdump-21 -d --demangle "$out/$variant/ikea_v3_series" > "$out/$variant/kernels.asm"
done
for trial in 1 2; do
  variants='stock vector'; if [[ "$trial" == 2 ]]; then variants='vector stock'; fi
  for variant in $variants; do
    taskset -c "$SIXDB_CPU" "$out/$variant/ikea_v3_series" \
      --benchmark_min_time=0.05s --benchmark_repetitions=5 \
      --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
      --benchmark_out="$out/$variant/trial-$trial.json" > "$out/$variant/trial-$trial.log" 2>&1
  done
done
