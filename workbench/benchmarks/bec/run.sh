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
build="build/benchmarks/bec/$profile"
mkdir -p "$SIXDB_RESULTS"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=bec -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build.resources" cmake --build "$build" \
  --target ikea_bec_bench ikea_bec4096_bench ikea_bec4096_sizes ikea_bec_check \
  ikea_bec4096_check ikea_bec_composition_check ikea_example_bec_bec4096 \
  -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
"$build/ikea/ikea_bec_check" > "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_bec_composition_check" >> "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_bec4096_check" >> "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_example_bec_bec4096" >> "$SIXDB_RESULTS/checks.log"
python3 ikea/test/headers.py "$build" --module bec >> "$SIXDB_RESULTS/checks.log"
cp "$build/workbench/benchmarks/bec/ikea_bec_bench" "$build/workbench/benchmarks/bec/ikea_bec4096_bench" \
  "$build/ikea/libikea_bec.a" \
  "$build/compile_commands.json" "$build/.ninja_log" "$SIXDB_RESULTS/"
llvm-size-21 "$SIXDB_RESULTS/ikea_bec_bench" "$SIXDB_RESULTS/libikea_bec.a" \
  > "$SIXDB_RESULTS/size.txt"
llvm-nm-21 -S --size-sort --demangle "$SIXDB_RESULTS/ikea_bec_bench" \
  > "$SIXDB_RESULTS/symbols.txt"
taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/ikea_bec_bench" \
  --benchmark_min_time="${BEC_MIN_TIME:-0.03}s" --benchmark_repetitions="${BEC_REPETITIONS:-3}" \
  --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
  --benchmark_out="$SIXDB_RESULTS/samples.json" "$@" > "$SIXDB_RESULTS/timings.log" 2>&1
# Bec4096 against sixteen Bec256 bodies, over synthetic groups and the retained
# RealRoaring windows; BEC4096=0 skips it.
if [[ ${BEC4096:-1} == 1 ]]; then
  windows=$(python3 workbench/tools/datasets.py fetch workbench/spikes/bec256-composition/windows.json)
  "$build/workbench/benchmarks/bec/ikea_bec4096_sizes" "$windows" > "$SIXDB_RESULTS/bec4096-sizes.txt"
  BEC_WINDOWS="$windows" taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/ikea_bec4096_bench" \
    --benchmark_min_time="${BEC_MIN_TIME:-0.03}s" --benchmark_repetitions="${BEC_REPETITIONS:-3}" \
    --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
    --benchmark_report_aggregates_only=true --benchmark_filter="${BEC4096_FILTER:-.}" \
    --benchmark_out="$SIXDB_RESULTS/bec4096-samples.json" > "$SIXDB_RESULTS/bec4096-timings.log" 2>&1
fi
