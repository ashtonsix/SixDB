#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose output or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
profiles=${TUPLE_PROFILES:-avx2 avx512}
for profile in $profiles; do
  case "$profile" in
    avx2) march=x86-64-v3; tune=zen5 ;;
    avx512) march=znver5; tune=zen5 ;;
    neon) march=armv8.2-a+simd; tune=neoverse-v2 ;;
    *) exit 2 ;;
  esac
  build="build/benchmarks/tuplepack/$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_BENCHMARKS=tuplepack -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune"
  /usr/bin/time -v -o "$out/build.resources" cmake --build "$build" --target \
    ikea_tuplepack_wire_check ikea_tuplepack_operations_check ikea_tuplepack_execution_check ikea_tuplepack_ownership_check ikea_tuplepack_packets_check ikea_example_tuplepack_packets ikea_tuplepack_bench \
    -j "${SIXDB_BUILD_JOBS:-1}"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_wire_check" > "$out/wire.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_operations_check" > "$out/operations.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_execution_check" > "$out/execution.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_ownership_check" > "$out/ownership.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_packets_check" > "$out/packets.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_example_tuplepack_packets" > "$out/packet-example.txt"
  cp "$build/ikea/libikea_tuplepack.a" "$build/workbench/benchmarks/tuplepack/ikea_tuplepack_bench" \
     "$build/compile_commands.json" "$build/.ninja_log" "$out/"
  llvm-size-21 "$out/libikea_tuplepack.a" "$out/ikea_tuplepack_bench" > "$out/size.txt"
  llvm-nm-21 --print-size --size-sort --demangle "$out/ikea_tuplepack_bench" > "$out/symbols.txt"
  taskset -c "$SIXDB_CPU" "$out/ikea_tuplepack_bench" \
    --benchmark_min_time="${TUPLE_MIN_TIME:-0.03}s" --benchmark_repetitions="${TUPLE_REPETITIONS:-3}" \
    --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
    --benchmark_out="$out/samples.json" "$@"
done
