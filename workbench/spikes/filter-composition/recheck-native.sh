#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
 aarch64) profile=neon; march=armv8-a+simd; tune=neoverse-v2 ;;
 *) profile=avx512; march=znver5; tune=zen5 ;;
esac
dir="build/filter-composition/$profile"
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=filter-composition
cmake --build "$dir" --target filter_composition_recheck -j "$SIXDB_BUILD_JOBS"
taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_composition_recheck" > "$SIXDB_RESULTS/$profile.csv" 2> "$SIXDB_RESULTS/$profile-runtime.txt"
llvm-objdump-21 -d --demangle "$dir/workbench/spikes/filter-composition/filter_composition_recheck" > "$SIXDB_RESULTS/$profile-disassembly.txt"
