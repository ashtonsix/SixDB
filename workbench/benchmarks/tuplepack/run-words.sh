#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose output or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
for profile in ${TUPLE_PROFILES:-avx2 avx512}; do
  case "$profile" in
    baseline) march=x86-64-v2; tune=zen5 ;;
    avx2) march=x86-64-v3; tune=zen5 ;;
    avx512) march=znver5; tune=zen5 ;;
    neon) march=armv8.2-a+simd; tune=neoverse-v2 ;;
    *) exit 2 ;;
  esac
  build="build/tuplepack/words-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_BENCHMARKS=tuplepack -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$out/configure.log" 2>&1
  /usr/bin/time -v -o "$out/build.resources" cmake --build "$build" \
    --target ikea_tuplepack_word_bench ikea_tuplepack_gpr_check ikea_tuplepack_packets_check \
      ikea_tuplepack_operations_check ikea_tuplepack_wire_check ikea_tuplepack_execution_check \
      ikea_example_tuplepack_words \
      -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build.log" 2>&1
  for check in gpr packets operations wire execution; do
    "$build/ikea/ikea_tuplepack_${check}_check" >> "$out/checks.log" 2>&1
  done
  "$build/ikea/ikea_example_tuplepack_words" >> "$out/checks.log" 2>&1
  python3 ikea/test/headers.py "$build" --module tuplepack >> "$out/checks.log" 2>&1
  cp "$build/workbench/benchmarks/tuplepack/ikea_tuplepack_word_bench" "$build/compile_commands.json" "$out/"
  llvm-size-21 "$out/ikea_tuplepack_word_bench" "$build/ikea/libikea_tuplepack.a" > "$out/size.txt"
  llvm-nm-21 -S --size-sort --demangle "$build/ikea/libikea_tuplepack.a" > "$out/symbols.txt"
  if [[ "$profile" == baseline ]]; then continue; fi
  TUPLEPACK_WORD_BROAD="${TUPLEPACK_WORD_BROAD:-1}" taskset -c "$SIXDB_CPU" "$out/ikea_tuplepack_word_bench" \
    --benchmark_min_time="${TUPLE_MIN_TIME:-0.005}s" --benchmark_repetitions="${TUPLE_REPETITIONS:-3}" \
    --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
    --benchmark_out="$out/samples.json" "$@" > "$out/timings.log" 2>&1
done
