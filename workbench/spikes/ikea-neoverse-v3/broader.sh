#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
for tune in neoverse-v2 neoverse-v3; do
  build="build/ikea-neoverse-v3/broader-$tune"
  mkdir -p "$out/$tune"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DSIXDB_BENCHMARKS=seriespack;tuplepack' -DSIXDB_SPIKES=ikea-neoverse-v3 \
    -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE="$tune" -DV3_SPIKE_TUNE="$tune" \
    > "$out/$tune/configure.log" 2>&1
  cmake --build "$build" --target ikea_seriespack_bench ikea_tuplepack_bench \
    ikea_tuplepack_word_bench ikea_v3_bench -j "$SIXDB_BUILD_JOBS" > "$out/$tune/build.log" 2>&1
  cp "$build/workbench/benchmarks/seriespack/ikea_seriespack_bench" "$out/$tune/"
  cp "$build/workbench/benchmarks/tuplepack/ikea_tuplepack_bench" "$out/$tune/"
  cp "$build/workbench/benchmarks/tuplepack/ikea_tuplepack_word_bench" "$out/$tune/"
  cp "$build/workbench/spikes/ikea-neoverse-v3/ikea_v3_bench" "$out/$tune/"
  cp "$build/compile_commands.json" "$out/$tune/"
  taskset -c "$SIXDB_CPU" "$out/$tune/ikea_v3_bench" --check > "$out/$tune/checks.txt"
  llvm-objdump-21 -d --demangle "$out/$tune/ikea_v3_bench" > "$out/$tune/kernels.asm"
done
for trial in 1 2; do
  tunes='neoverse-v2 neoverse-v3'; if [[ "$trial" == 2 ]]; then tunes='neoverse-v3 neoverse-v2'; fi
  for tune in $tunes; do
    for suite in seriespack tuplepack words constrained; do
      args=()
      case "$suite" in
        seriespack) binary=ikea_seriespack_bench; args+=(--suite=all);;
        tuplepack) binary=ikea_tuplepack_bench;;
        words) binary=ikea_tuplepack_word_bench; args+=('--benchmark_filter=^word/(1|2|4)/unit(4|16|64)/stride(4|16|64)/map(0|2)/random/all/(word_hash|update)/(gpr|gpr_native|simd|simd_word)$');;
        constrained) binary=ikea_v3_bench; args+=('--benchmark_filter=^pair/(random|population16)/(exact|padded)/(maintained|interleaved|constrained)$');;
      esac
      TUPLEPACK_WORD_BROAD=1 taskset -c "$SIXDB_CPU" "$out/$tune/$binary" "${args[@]}" \
        --benchmark_min_time=0.03s --benchmark_repetitions=5 \
        --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
        --benchmark_out="$out/$tune/$suite-$trial.json" > "$out/$tune/$suite-$trial.log" 2>&1
    done
  done
done
