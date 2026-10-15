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
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build="build/hashtable-growth-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_growth -j "${SIXDB_BUILD_JOBS:-1}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
dir="$build/workbench/spikes/hashtable/reference"
taskset -c "$SIXDB_CPU" "$dir/hashtable_growth" check > "$SIXDB_RESULTS/check.txt"
cp "$dir/hashtable_growth" "$SIXDB_RESULTS/hashtable_growth"
llvm-objdump-21 -d --demangle "$dir/hashtable_growth" | gzip > "$SIXDB_RESULTS/assembly.gz"
cp "$dir/table-plans.h" "$SIXDB_RESULTS/table-plans.h"
cp "$dir/single-plans.h" "$SIXDB_RESULTS/single-plans.h"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
# Each repetition rotates the four arms and uses 128 fresh caller seed bundles.
# Read loops are hot and answer-dependent; this is not a cold-residence panel.
/usr/bin/time -v -o "$SIXDB_RESULTS/operations.resources" \
  taskset -c "$SIXDB_CPU" "$dir/hashtable_growth" 128 8 > "$SIXDB_RESULTS/operations.csv"
