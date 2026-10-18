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
  dir="build/filter-early/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=packed-filters
  cmake --build "$dir" --target packed_filters_early packed_filters_early_guards -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/packed-filters/packed_filters_early_guards" \
    > "$SIXDB_RESULTS/$profile-guards.txt" 2>&1
  exe="$dir/workbench/spikes/packed-filters/packed_filters_early"
  taskset -c "$SIXDB_CPU" "$exe" --iterations "${EARLY_ITERATIONS:-30000}" --repeats "${EARLY_REPEATS:-5}" "$@" \
    > "$SIXDB_RESULTS/$profile.csv" 2> "$SIXDB_RESULTS/$profile-check.txt"
  python3 workbench/spikes/packed-filters/early-report.py "$SIXDB_RESULTS/$profile.csv" \
    "$SIXDB_RESULTS/$profile-summary.csv"
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-disassembly.txt"
  llvm-size-21 -A "$exe" > "$SIXDB_RESULTS/$profile-size.txt"
  cp "$exe" "$SIXDB_RESULTS/$profile-binary"
done
