#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose output or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
for profile in ${TUPLE_PROFILES:-avx2 avx512}; do
  case "$profile" in
    avx2) march=x86-64-v3; tune=zen5 ;;
    avx512) march=znver5; tune=zen5 ;;
    neon) march=armv8.2-a+simd; tune=neoverse-v2 ;;
    *) exit 2 ;;
  esac
  build="build/tuple-layout/crossover-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=tuple-layout -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$out/configure.log" 2>&1
  /usr/bin/time -v -o "$out/build.resources" cmake --build "$build" \
    --target ikea_tuplepack_packets_check tuple_packet_crossover -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build.log" 2>&1
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_packets_check" > "$out/check.txt"
  cp "$build/workbench/spikes/tuple-layout/tuple_packet_crossover" "$build/compile_commands.json" "$out/"
  llvm-size-21 "$out/tuple_packet_crossover" > "$out/size.txt"
  taskset -c "$SIXDB_CPU" "$out/tuple_packet_crossover" \
    --benchmark_min_time="${TUPLE_MIN_TIME:-0.01}s" --benchmark_repetitions="${TUPLE_REPETITIONS:-3}" \
    --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
    --benchmark_out="$out/samples.json" "$@" > "$out/timings.log" 2>&1
done
