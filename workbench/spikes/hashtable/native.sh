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
build="build/hashtable-design-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_design_compare -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
exe="$build/workbench/spikes/hashtable/hashtable_design_compare"
taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check.txt"
cp "$exe" "$SIXDB_RESULTS/hashtable_design_compare"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
llvm-objdump-21 -d --demangle "$exe" | gzip > "$SIXDB_RESULTS/assembly.gz"
python3 workbench/spikes/hashtable/check_codegen.py "$SIXDB_RESULTS/assembly.gz" \
  > "$SIXDB_RESULTS/codegen.json"
/usr/bin/time -v -o "$SIXDB_RESULTS/resources.txt" \
  taskset -c "$SIXDB_CPU" "$exe" "${HASHTABLE_PANEL:-bench}" "${HASHTABLE_WORK:-1000}" \
  "${HASHTABLE_REPEATS:-12}" > "$SIXDB_RESULTS/operations.csv"
