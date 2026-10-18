#!/bin/bash
set -euo pipefail
dir=build/filter-tiles/arm-index
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2 -DSIXDB_SPIKES=filter-composition
cmake --build "$dir" --target filter_arm_index -j "$SIXDB_BUILD_JOBS"
exe="$dir/workbench/spikes/filter-composition/filter_arm_index"
for order in 0 1; do
  taskset -c "$SIXDB_CPU" "$exe" 80000 5 "$order" > "$SIXDB_RESULTS/index-$order.csv" 2> "$SIXDB_RESULTS/index-$order-check.txt"
done
llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/index-assembly.txt"
clang++-21 -std=c++23 -O3 -fno-vectorize -fno-slp-vectorize -mcpu=neoverse-v2 \
  workbench/spikes/filter-composition/contains-arm.cpp -o "$SIXDB_RESULTS/contains-arm"
taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/contains-arm" > "$SIXDB_RESULTS/contains-check.txt"
llvm-objdump-21 -d --no-show-raw-insn --disassemble-symbols=contains6_arm_prepared,contains6_arm_runtime,contains6_sve_prepared \
  "$SIXDB_RESULTS/contains-arm" > "$SIXDB_RESULTS/contains-assembly.txt"
