#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cp /proc/meminfo "$SIXDB_RESULTS/meminfo.txt"
if [[ $(uname -m) == aarch64 ]]; then tune=neoverse-v2;variants=(neon)
else tune=${HASHTABLE_TUNE:?};variants=(avx2 avx512);fi
for isa in "${variants[@]}"; do
  march=armv8-a+simd
  if [[ $isa == avx2 ]]; then march=x86-64-v3;fi
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5;else march=graniterapids;fi
  fi
  build="build/hashtable-layout-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_compare hashtable_width_check -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  exe="$build/workbench/spikes/hashtable/reference/hashtable_compare"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check-$isa.txt"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/reference/hashtable_width_check" > "$SIXDB_RESULTS/width-check-$isa.txt"
  llvm-objdump-21 -d --demangle "$exe" | gzip > "$SIXDB_RESULTS/assembly-$isa.txt.gz"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  cp "$exe" "$SIXDB_RESULTS/compare-$isa"
  sha256sum "$exe" > "$SIXDB_RESULTS/binary-$isa.sha256"
  for n in ${HASHTABLE_SIZES:-4096 1048576 4194304}; do
    for experiment in ${HASHTABLE_CASES:-metadata64 prefetch64 capacities64}; do
      stem="$SIXDB_RESULTS/$experiment-$isa-$n"
      /usr/bin/time -v -o "$stem.resources" taskset -c "$SIXDB_CPU" "$exe" "$experiment" "$n" "${HASHTABLE_REPS:-5}" > "$stem.csv"
    done
  done
  if [[ ${HASHTABLE_QUOTIENT:-0} == 1 ]]; then
    taskset -c "$SIXDB_CPU" "$exe" quotient16 7 > "$SIXDB_RESULTS/quotient16-$isa.csv"
  fi
done
