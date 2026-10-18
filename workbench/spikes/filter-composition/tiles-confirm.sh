#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles=neon; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${FILTER_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/filter-tiles/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=filter-composition
  cmake --build "$dir" --target filter_tiles filter_indexof ikea_filters_check -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_filters_check" > "$SIXDB_RESULTS/$profile-regression.txt"
  for order in 0 1; do
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_tiles" 100000 5 "$order" \
      > "$SIXDB_RESULTS/$profile-tiles-$order.csv" 2> "$SIXDB_RESULTS/$profile-tiles-$order-check.txt"
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_indexof" 100000 5 "$order" \
      > "$SIXDB_RESULTS/$profile-index-$order.csv" 2> "$SIXDB_RESULTS/$profile-index-$order-check.txt"
  done
  llvm-objdump-21 -d --demangle "$dir/workbench/spikes/filter-composition/filter_indexof" > "$SIXDB_RESULTS/$profile-index-assembly.txt"
done
