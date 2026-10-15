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
    avx512) march=x86-64-v4 ;;
  esac
  # TuplePack's full ZMM carrier requires VBMI; target the actual hosts here.
  if [ "$profile" = avx512 ]; then
    if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi
  fi
  dir="build/packed-filters/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=packed-filters
  cmake --build "$dir" --target packed_filters_bench ikea_filters_check -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_filters_check" > "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/packed-filters/packed_filters_bench" 300000 5 \
    > "$SIXDB_RESULTS/$profile.csv"
  llvm-objdump-21 -d --demangle "$dir/workbench/spikes/packed-filters/packed_filters_bench" \
    > "$SIXDB_RESULTS/$profile-disassembly.txt"
  python3 workbench/spikes/packed-filters/audit.py "$SIXDB_RESULTS/$profile-disassembly.txt" "$SIXDB_RESULTS/$profile-fusion.json"
  clang++-21 -std=c++23 -O3 -fno-vectorize -fno-slp-vectorize -march="$march" \
    -Iikea/include -S workbench/spikes/packed-filters/codegen.cpp -o "$SIXDB_RESULTS/$profile-codegen.s"
  python3 workbench/spikes/packed-filters/mca.py "$SIXDB_RESULTS/$profile-codegen.s" "$tune" \
    "$SIXDB_RESULTS/$profile-mca"
done
