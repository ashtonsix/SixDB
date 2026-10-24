#!/bin/bash
# Short planes outside L1/L2: whole-plane counts versus bisection, by footprint.
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune=neoverse-v2 ;;
 *) profiles="avx2 avx512"; tune=zen5 ;;
esac
for profile in $profiles; do
 case "$profile" in neon) march=armv8-a+simd ;; avx2) march=x86-64-v3 ;; avx512) march=znver5 ;; esac
 dir="build/intervals-search/$profile"; out="$SIXDB_RESULTS/$profile"; mkdir -p "$out"
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only -falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6' \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=intervals-search > "$out/configure.log"
 cmake --build "$dir" --target intervals_search_footprint -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build.log" 2>&1
 for bytes in 262144 8388608 67108864 268435456; do
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/intervals-search/intervals_search_footprint" "$bytes" 7 >> "$out/footprint.jsonl"
 done
done
