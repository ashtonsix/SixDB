#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cat /sys/kernel/mm/transparent_hugepage/enabled > "$SIXDB_RESULTS/thp-policy.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5;else march=graniterapids;fi
  fi
fi
build="build/hashtable-table-ring-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_table_ring -j "${SIXDB_BUILD_JOBS:-1}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
dir="$build/workbench/spikes/hashtable/reference"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_ring" 5 701 2 1 8 1 37913 \
  > "$SIXDB_RESULTS/check.csv" 2> "$SIXDB_RESULTS/check.log"
llvm-objdump-21 -d --demangle "$dir/hashtable_table_ring" | gzip > "$SIXDB_RESULTS/assembly.txt.gz"
cp "$dir/hashtable_table_ring" "$SIXDB_RESULTS/table-ring"
cp "$dir/table-plans.h" "$SIXDB_RESULTS/table-plans.h"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
# One balanced seven-arm screen. Candidate/Abseil share history and queries;
# it fixes whole-bucket hinting and does not select the scheduling policy.
for specification in \
  'hot_serial 8 3276 8 1 1' \
  'hot_parallel 8 3276 8 1 32' \
  'large_fresh_parallel 20 13421772 0 0 32' \
  'large_aged_parallel 20 13421772 8 1 32' \
  'large_aged_serial 20 13421772 8 1 1'; do
  read -r name a entries turns mixed concurrency <<< "$specification"
  /usr/bin/time -v -o "$SIXDB_RESULTS/$name.resources" \
    taskset -c "$SIXDB_CPU" "$dir/hashtable_table_ring" \
      "$a" "$entries" "$turns" "$mixed" "$concurrency" 7 37913 \
    > "$SIXDB_RESULTS/$name.csv" 2> "$SIXDB_RESULTS/$name.log"
done
