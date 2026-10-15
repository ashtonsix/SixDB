#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then tune=neoverse-v2;variants=(neon)
else tune=${HASHTABLE_TUNE:?};variants=(avx2 avx512);fi
for isa in "${variants[@]}"; do
  march=armv8-a+simd
  if [[ $isa == avx2 ]]; then march=x86-64-v3;fi
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5;else march=graniterapids;fi
  fi
  build="build/hashtable-table-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_table_compare -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  dir="$build/workbench/spikes/hashtable/reference"
  taskset -c "$SIXDB_CPU" "$dir/hashtable_table_compare" check > "$SIXDB_RESULTS/check-$isa.txt"
  llvm-objdump-21 -d --demangle "$dir/hashtable_table_compare" | gzip > "$SIXDB_RESULTS/assembly-$isa.txt.gz"
  cp "$dir/hashtable_table_compare" "$SIXDB_RESULTS/table-$isa"
  cp "$dir/table-plans.h" "$SIXDB_RESULTS/table-plans-$isa.h"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  # Eight rotations balance the four selected arms twice. The two occupancies
  # distinguish useful aging from a tail-pressure stress; neither selects growth.
  for entries in 3276 3800; do
    /usr/bin/time -v -o "$SIXDB_RESULTS/maintenance-$isa-$entries.resources" \
      taskset -c "$SIXDB_CPU" "$dir/hashtable_table_compare" "$entries" 8 maintenance \
      > "$SIXDB_RESULTS/maintenance-$isa-$entries.csv"
  done
done
