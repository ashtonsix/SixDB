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
  build="build/hashtable-single-lifecycle-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_single_lifecycle hashtable_single_check -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  dir="$build/workbench/spikes/hashtable/reference"
  taskset -c "$SIXDB_CPU" "$dir/hashtable_single_check" > "$SIXDB_RESULTS/check-single-$isa.txt"
  taskset -c "$SIXDB_CPU" "$dir/hashtable_single_lifecycle" check > "$SIXDB_RESULTS/check-lifecycle-$isa.txt"
  llvm-objdump-21 -d --demangle "$dir/hashtable_single_lifecycle" | gzip > "$SIXDB_RESULTS/assembly-$isa.txt.gz"
  cp "$dir/hashtable_single_lifecycle" "$SIXDB_RESULTS/lifecycle-$isa"
  cp "$dir/single-plans.h" "$SIXDB_RESULTS/single-plans-$isa.h"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  for objects in 256 4096; do
    /usr/bin/time -v -o "$SIXDB_RESULTS/lifecycle-$isa-$objects.resources" \
      taskset -c "$SIXDB_CPU" "$dir/hashtable_single_lifecycle" "$objects" 6 > "$SIXDB_RESULTS/lifecycle-$isa-$objects.csv"
  done
done
