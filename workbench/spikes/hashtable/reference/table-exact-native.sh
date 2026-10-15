#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5;else march=graniterapids;fi
  fi
fi
build="build/hashtable-table-exact-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_table_compare hashtable_table_ring hashtable_table_exact_ring \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/build.log" 2>&1
dir="$build/workbench/spikes/hashtable/reference"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_compare" exact-check > "$SIXDB_RESULTS/check.txt"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_exact_ring" 5 701 2 1 8 1 37913 \
  > "$SIXDB_RESULTS/ring-check.csv" 2> "$SIXDB_RESULTS/ring-check.log"
for target in hashtable_table_compare hashtable_table_ring hashtable_table_exact_ring; do
  cp "$dir/$target" "$SIXDB_RESULTS/$target"
  llvm-objdump-21 -d --demangle "$dir/$target" | gzip > "$SIXDB_RESULTS/$target.assembly.gz"
done
cp "$dir/table-plans.h" "$SIXDB_RESULTS/table-plans.h"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
/usr/bin/time -v -o "$SIXDB_RESULTS/operations.resources" \
  taskset -c "$SIXDB_CPU" "$dir/hashtable_table_compare" 3276 6 exact > "$SIXDB_RESULTS/operations.csv"
# Matched complete cold execution; both formats include ordinary Abseil and
# public-prefetch controls. Rotate format order across distinct workload cases.
order=0
for specification in \
  'hot_hit_serial 8 3276 0 0 1' \
  'hot_aged_serial 8 3276 8 1 1' \
  'large_fresh_parallel 20 13421772 0 0 32' \
  'large_aged_parallel 20 13421772 8 1 32'; do
  read -r name a entries turns mixed concurrency <<< "$specification"
  formats=(rows exact)
  if (( order%2 )); then formats=(exact rows); fi
  for format in "${formats[@]}"; do
    target=hashtable_table_ring
    if [[ $format == exact ]]; then target=hashtable_table_exact_ring; fi
    /usr/bin/time -v -o "$SIXDB_RESULTS/$name-$format.resources" \
      taskset -c "$SIXDB_CPU" "$dir/$target" \
      "$a" "$entries" "$turns" "$mixed" "$concurrency" 7 37913 \
      > "$SIXDB_RESULTS/$name-$format.csv" 2> "$SIXDB_RESULTS/$name-$format.log"
  done
  order=$((order+1))
done
