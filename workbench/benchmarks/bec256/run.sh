#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose output}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
profile="${1:-avx512}"
if (($#)); then shift; fi
case "$profile" in
  avx512) march=znver5; tune=zen5;;
  avx2) march=x86-64-v3; tune=zen5;;
  granite) march=graniterapids; tune=granite-rapids;;
  neon) march=armv8.2-a+simd; tune=neoverse-v2;;
  *) exit 2;;
esac
build="build/benchmarks/bec256/$profile"
mkdir -p "$SIXDB_RESULTS"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=bec256 -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build.resources" cmake --build "$build" \
  --target ikea_bec256_bench ikea_bec256_check ikea_bec256_composition_check \
  -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
"$build/ikea/ikea_bec256_check" > "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_bec256_composition_check" >> "$SIXDB_RESULTS/checks.log"
python3 ikea/test/headers.py "$build" --module bec256 >> "$SIXDB_RESULTS/checks.log"
cp "$build/workbench/benchmarks/bec256/ikea_bec256_bench" "$build/ikea/libikea_bec256.a" \
  "$build/compile_commands.json" "$build/.ninja_log" "$SIXDB_RESULTS/"
llvm-size-21 "$SIXDB_RESULTS/ikea_bec256_bench" "$SIXDB_RESULTS/libikea_bec256.a" \
  > "$SIXDB_RESULTS/size.txt"
llvm-nm-21 -S --size-sort --demangle "$SIXDB_RESULTS/ikea_bec256_bench" \
  > "$SIXDB_RESULTS/symbols.txt"
taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/ikea_bec256_bench" \
  --benchmark_min_time="${BEC_MIN_TIME:-0.03}s" --benchmark_repetitions="${BEC_REPETITIONS:-3}" \
  --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
  --benchmark_out="$SIXDB_RESULTS/samples.json" "$@" > "$SIXDB_RESULTS/timings.log" 2>&1
