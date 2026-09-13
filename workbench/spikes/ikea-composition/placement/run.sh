#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or provide an output directory}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
profile=${PLACEMENT_PROFILE:-avx512}
case "$profile" in
  avx512) march=znver5; tune=zen5 ;;
  avx2) march=x86-64-v3; tune=zen5 ;;
  neon) march=armv8-a+simd; tune=neoverse-v2 ;;
esac
build="build/ikea-placement/$profile"
mkdir -p "$SIXDB_RESULTS"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=ikea-composition -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build.resources" \
  cmake --build "$build" --target seriespack_placement_probe -j "${SIXDB_BUILD_JOBS:-1}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
binary="$build/workbench/spikes/ikea-composition/seriespack_placement_probe"
cp "$binary" "$build/compile_commands.json" "$SIXDB_RESULTS/"
taskset -c "$SIXDB_CPU" "$binary" --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
  --benchmark_out="$SIXDB_RESULTS/samples.json" "$@" > "$SIXDB_RESULTS/timings.log" 2>&1
