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
  isa=avx2; march=x86-64-v3; tune=${HASHTABLE_TUNE:?}
fi
build="build/hashtable-values-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_table_values hashtable_table_ring \
  -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
dir="$build/workbench/spikes/hashtable/reference"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_values" check > "$SIXDB_RESULTS/check.txt"
for program in hashtable_table_values hashtable_table_ring; do
  cp "$dir/$program" "$SIXDB_RESULTS/$program"
  llvm-objdump-21 -d --demangle "$dir/$program" | gzip > "$SIXDB_RESULTS/$program.assembly.gz"
done
cp "$dir/table-plans.h" "$SIXDB_RESULTS/table-plans.h"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_ring" 8 3276 8 1 1 7 37913 \
  > "$SIXDB_RESULTS/word-sentinel.csv" 2> "$SIXDB_RESULTS/word-sentinel.log"
for specification in '8 3276' '18 3355443'; do
  read -r a entries <<< "$specification"
  /usr/bin/time -v -o "$SIXDB_RESULTS/values-$entries.resources" \
    taskset -c "$SIXDB_CPU" "$dir/hashtable_table_values" "$a" "$entries" 6 \
    > "$SIXDB_RESULTS/values-$entries.csv"
done
