#!/bin/bash
set -euo pipefail
if [ "$(uname -m)" = aarch64 ]; then
  profiles=neon; tune=neoverse-v2
  bash workbench/spikes/filter-composition/arm-index-native.sh
else
  profiles='avx2 avx512'; tune="${FILTER_TUNE:-zen5}"
fi
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/filter-tiles/arm-graduate-$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune"
  cmake --build "$dir" --target ikea_filters_check ikea_example_filters_ordinary -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_filters_check" > "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_example_filters_ordinary" > "$SIXDB_RESULTS/$profile-example.txt"
done
