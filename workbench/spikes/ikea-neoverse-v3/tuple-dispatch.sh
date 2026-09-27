#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
for variant in stock dispatch; do
  build="build/ikea-neoverse-v3/tuple-$variant"
  mkdir -p "$out/$variant"
  split=OFF; if [[ "$variant" == dispatch ]]; then split=ON; fi
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=ikea-neoverse-v3 -DSIXDB_BENCHMARKS=tuplepack \
    -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE=neoverse-v2 \
    -DV3_TUPLE_DISPATCH="$split" > "$out/$variant/configure.log" 2>&1
  cmake --build "$build" --target ikea_tuplepack_bench ikea_tuplepack_word_bench \
    ikea_tuplepack_gpr_check ikea_tuplepack_packets_check ikea_tuplepack_wire_check \
    ikea_tuplepack_operations_check ikea_tuplepack_execution_check -j "$SIXDB_BUILD_JOBS" \
    > "$out/$variant/build.log" 2>&1
  for check in gpr packets wire operations execution; do
    taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_${check}_check" >> "$out/$variant/checks.txt"
  done
  for target in ikea_tuplepack_bench ikea_tuplepack_word_bench; do
    cp "$build/workbench/benchmarks/tuplepack/$target" "$out/$variant/"
  done
  cp "$build/compile_commands.json" "$out/$variant/"
  llvm-objdump-21 -d --demangle "$build/ikea/libikea_tuplepack.a" > "$out/$variant/kernels.asm"
done
for trial in 1 2; do
  variants='stock dispatch'; if [[ "$trial" == 2 ]]; then variants='dispatch stock'; fi
  for variant in $variants; do
    taskset -c "$SIXDB_CPU" "$out/$variant/ikea_tuplepack_bench" \
      --benchmark_min_time=0.03s --benchmark_repetitions=5 \
      --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
      --benchmark_out="$out/$variant/tuple-$trial.json" > "$out/$variant/tuple-$trial.log" 2>&1
    TUPLEPACK_WORD_BROAD=1 taskset -c "$SIXDB_CPU" "$out/$variant/ikea_tuplepack_word_bench" \
      '--benchmark_filter=^word/(1|2|4)/unit(4|16|64)/stride(4|16|64)/map(0|2)/random/all/(word_hash|update)/(gpr|gpr_native|simd|simd_word)$' \
      --benchmark_min_time=0.03s --benchmark_repetitions=5 \
      --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
      --benchmark_out="$out/$variant/words-$trial.json" > "$out/$variant/words-$trial.log" 2>&1
  done
done
