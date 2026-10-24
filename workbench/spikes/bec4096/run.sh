#!/bin/bash
# Checks, sizes and timings on a worker: worker.py run THIS --machine M --arg PROFILE
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose output}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
profile="${1:-avx512}"
case "$profile" in
  avx512) march=znver5; tune=zen5;;
  granite) march=graniterapids; tune=granite-rapids;;
  neon) march=armv8.2-a+simd; tune=neoverse-v2;;
  *) exit 2;;
esac
build="build/bec4096/$profile"
mkdir -p "$SIXDB_RESULTS"
# Fixed code alignment keeps unrelated layout changes out of arm comparisons.
align="-falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=bec4096 \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" "-DCMAKE_CXX_FLAGS=$align" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target bec4096_check bec4096_sizes bec4096_bench \
  -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
dir="$build/workbench/spikes/bec4096"
"$dir/bec4096_check" > "$SIXDB_RESULTS/checks.log"
windows=$(python3 workbench/tools/datasets.py fetch workbench/spikes/bec256-composition/windows.json)
"$dir/bec4096_sizes" "$windows" > "$SIXDB_RESULTS/sizes.txt"
cp "$dir/bec4096_bench" "$build/compile_commands.json" "$SIXDB_RESULTS/"
BEC_WINDOWS="$windows" taskset -c "$SIXDB_CPU" "$dir/bec4096_bench" \
  --benchmark_min_time="${BEC_MIN_TIME:-0.05}s" --benchmark_repetitions="${BEC_REPETITIONS:-5}" \
  --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
  --benchmark_out="$SIXDB_RESULTS/samples.json" --benchmark_report_aggregates_only=true \
  --benchmark_filter="${BEC_FILTER:-.}" > "$SIXDB_RESULTS/timings.log" 2>&1
