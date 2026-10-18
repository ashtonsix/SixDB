#!/bin/bash
# Native filter regression checks; worker.py supplies CPU, results and jobs.
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles="neon"; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${SIXDB_FILTER_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512)
      if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/ikea/filters/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=
  cmake --build "$dir" --target ikea_filters_check \
    ikea_example_filters_ordinary ikea_example_filters_composition ikea_example_filters_guarded \
    -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" ctest --test-dir "$dir" --output-on-failure --no-tests=error \
    -R '^ikea_(filters_check|example_filters_.*)$' > "$SIXDB_RESULTS/$profile-check.txt"
  llvm-objdump-21 -d --demangle "$dir/ikea/ikea_example_filters_composition" \
    > "$SIXDB_RESULTS/$profile-composition.txt"
done
