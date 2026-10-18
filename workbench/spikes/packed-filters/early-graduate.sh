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
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=packed-filters
  cmake --build "$dir" --target ikea_filters_check ikea_example_filters_ordinary \
    ikea_example_filters_composition ikea_example_filters_guarded packed_filters_early_graduate \
    -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" ctest --test-dir "$dir" --output-on-failure --no-tests=error \
    -R '^ikea_(filters_check|example_filters_.*)$' > "$SIXDB_RESULTS/$profile-check.txt"
  exe="$dir/workbench/spikes/packed-filters/packed_filters_early_graduate"
  taskset -c "$SIXDB_CPU" "$exe" > "$SIXDB_RESULTS/$profile.csv" 2> "$SIXDB_RESULTS/$profile-oracle.txt"
  python3 workbench/spikes/packed-filters/early-report.py "$SIXDB_RESULTS/$profile.csv" \
    "$SIXDB_RESULTS/$profile-summary.csv"
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-disassembly.txt"
  python3 workbench/spikes/packed-filters/audit.py "$SIXDB_RESULTS/$profile-disassembly.txt" \
    "$SIXDB_RESULTS/$profile-audit.json"
  cp "$exe" "$SIXDB_RESULTS/$profile-binary"
done
