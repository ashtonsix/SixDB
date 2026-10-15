#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build="build/hashtable-table-word-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_table_compare hashtable_table_ring \
  hashtable_table_exact_ring hashtable_table_word_ring -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
dir="$build/workbench/spikes/hashtable/reference"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_compare" word-check > "$SIXDB_RESULTS/check.txt"
taskset -c "$SIXDB_CPU" "$dir/hashtable_table_word_ring" 8 5000 2 1 8 1 37913 \
  > "$SIXDB_RESULTS/ring-check.csv" 2> "$SIXDB_RESULTS/ring-check.log"
for target in hashtable_table_compare hashtable_table_ring hashtable_table_exact_ring hashtable_table_word_ring; do
  cp "$dir/$target" "$SIXDB_RESULTS/$target"
  llvm-objdump-21 -d --demangle "$dir/$target" | gzip > "$SIXDB_RESULTS/$target.assembly.gz"
done
cp "$dir/table-plans.h" "$SIXDB_RESULTS/table-plans.h"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
/usr/bin/time -v -o "$SIXDB_RESULTS/operations.resources" \
  taskset -c "$SIXDB_CPU" "$dir/hashtable_table_compare" 3276 5 words > "$SIXDB_RESULTS/operations.csv"
# Three separately constructed seeds. Rotate the format order each epoch;
# each process also balances its seven execution arms. Hot R32 only: widening
# R20 to a native word would change the cold footprint and is a separate choice.
formats=(rows planes words)
for epoch in 0 1 2; do
  seed=$((37913+epoch*337))
  for specification in 'fresh 0 0' 'aged 8 1'; do
    read -r name turns mixed <<< "$specification"
    for index in 0 1 2; do
      format=${formats[$(((index+epoch)%3))]}
      target=hashtable_table_ring
      if [[ $format == planes ]]; then target=hashtable_table_exact_ring; fi
      if [[ $format == words ]]; then target=hashtable_table_word_ring; fi
      taskset -c "$SIXDB_CPU" "$dir/$target" 8 3276 "$turns" "$mixed" 1 7 "$seed" \
        > "$SIXDB_RESULTS/$name-$format-$epoch.csv" 2> "$SIXDB_RESULTS/$name-$format-$epoch.log"
    done
  done
done
