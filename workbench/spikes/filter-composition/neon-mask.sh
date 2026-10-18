#!/bin/bash
set -euo pipefail
dir=build/filter-k32/native
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2 -DSIXDB_SPIKES=filter-composition
cmake --build "$dir" --target filter_composition_neon_mask -j "$SIXDB_BUILD_JOBS"
exe="$dir/workbench/spikes/filter-composition/filter_composition_neon_mask"
taskset -c "$SIXDB_CPU" "$exe" > "$SIXDB_RESULTS/neon.csv" 2> "$SIXDB_RESULTS/check.txt"
llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/disassembly.txt"
cp "$exe" "$SIXDB_RESULTS/binary"
