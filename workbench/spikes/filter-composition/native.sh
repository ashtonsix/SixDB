#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles="neon"; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${FILTER_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/filter-composition/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=filter-composition
  cmake --build "$dir" --target filter_composition_runtime -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_composition_runtime" --check \
    > "$SIXDB_RESULTS/$profile-check.txt" 2>&1
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_composition_runtime" 100000 5 \
    > "$SIXDB_RESULTS/$profile.csv" 2> "$SIXDB_RESULTS/$profile-runtime.txt"
  llvm-objdump-21 -d --demangle "$dir/workbench/spikes/filter-composition/filter_composition_runtime" \
    > "$SIXDB_RESULTS/$profile-disassembly.txt"
  llvm-size-21 -A "$dir/workbench/spikes/filter-composition/filter_composition_runtime" \
    > "$SIXDB_RESULTS/$profile-size.txt"
done
