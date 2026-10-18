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
  cmake --build "$dir" --target filter_tiles_sweep filter_tile_codegen ikea_filters_check -j "$SIXDB_BUILD_JOBS"
  exe="$dir/workbench/spikes/filter-composition/filter_tiles_sweep"
  taskset -c "$SIXDB_CPU" "$exe" 40000 5 > "$SIXDB_RESULTS/$profile.csv" 2> "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$exe" 40000 5 1 > "$SIXDB_RESULTS/$profile-reverse.csv" 2> "$SIXDB_RESULTS/$profile-reverse-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_filters_check" > "$SIXDB_RESULTS/$profile-regression.txt"
  obj="$dir/workbench/spikes/filter-composition/CMakeFiles/filter_tile_codegen.dir/tile-codegen.cpp.o"
  case "$tune" in zen5) model=znver5 ;; granite-rapids) model=graniterapids ;; *) model=neoverse-v2 ;; esac
  python3 workbench/spikes/filter-composition/tile-audit.py "$dir" "$SIXDB_RESULTS" "$profile" "$model"
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-assembly.txt"
  cp "$exe" "$SIXDB_RESULTS/$profile-binary"
done
