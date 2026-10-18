#!/bin/bash
set -euo pipefail
dir=build/filter-mask-graduation/baseline
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2 '-DSIXDB_SPIKES=filter-composition;packed-filters'
cmake --build "$dir" --target packed_filters_bench filter_mask_codegen -j "$SIXDB_BUILD_JOBS"
exe="$dir/workbench/spikes/packed-filters/packed_filters_bench"
taskset -c "$SIXDB_CPU" "$exe" 100000 5 > "$SIXDB_RESULTS/neon.csv" 2> "$SIXDB_RESULTS/check.txt"
llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/disassembly.txt"
llvm-objdump-21 -d --demangle \
  "$dir/workbench/spikes/filter-composition/CMakeFiles/filter_mask_codegen.dir/mask-codegen.cpp.o" \
  > "$SIXDB_RESULTS/masks.txt"
python3 workbench/spikes/filter-composition/mask-audit.py "$SIXDB_RESULTS/masks.txt" "$SIXDB_RESULTS/masks-audit.json"
cp "$exe" "$SIXDB_RESULTS/binary"
