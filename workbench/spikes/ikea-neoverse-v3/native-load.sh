#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
for variant in control sve; do
  build="build/ikea-neoverse-v3/native-load-$variant"
  mkdir -p "$out/$variant"
  control=OFF; if [[ "$variant" == control ]]; then control=ON; fi
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=ikea-neoverse-v3 -DSIXDB_BENCHMARKS=bec256 \
    -DSIXDB_MARCH=armv8.2-a+sve -DSIXDB_TUNE=neoverse-v3 -DV3_SPIKE_TUNE=neoverse-v3 \
    -DV3_DISABLE_NATIVE_SVE="$control" > "$out/$variant/configure.log" 2>&1
  cmake --build "$build" --target ikea_bec256_bench ikea_v3_bench \
    ikea_bec256_check ikea_bec256_composition_check -j "$SIXDB_BUILD_JOBS" \
    > "$out/$variant/build.log" 2>&1
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_bec256_check" > "$out/$variant/checks.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_bec256_composition_check" >> "$out/$variant/checks.txt"
  cp "$build/workbench/benchmarks/bec256/ikea_bec256_bench" "$out/$variant/"
  cp "$build/workbench/spikes/ikea-neoverse-v3/ikea_v3_bench" "$out/$variant/"
  cp "$build/compile_commands.json" "$out/$variant/"
  llvm-objdump-21 -d --demangle "$out/$variant/ikea_bec256_bench" > "$out/$variant/kernels.asm"
done
for trial in 1 2; do
  variants='control sve'; if [[ "$trial" == 2 ]]; then variants='sve control'; fi
  for variant in $variants; do
    for suite in maintained shapes; do
      binary=ikea_bec256_bench; args=()
      if [[ "$suite" == shapes ]]; then binary=ikea_v3_bench; args+=('--benchmark_filter=^pair/.*/maintained$'); fi
      taskset -c "$SIXDB_CPU" "$out/$variant/$binary" "${args[@]}" \
        --benchmark_min_time=0.05s --benchmark_repetitions=7 \
        --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
        --benchmark_out="$out/$variant/$suite-$trial.json" > "$out/$variant/$suite-$trial.log" 2>&1
    done
  done
done
