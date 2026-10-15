#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}
  march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build="build/hashtable-design-migration-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target hashtable_design_migration -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
exe="$build/workbench/spikes/hashtable/hashtable_design_migration"
cp "$exe" "$SIXDB_RESULTS/migration"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/assembly.gz"
llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/symbols.txt"
taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check.txt"
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" \
  taskset -c "$SIXDB_CPU" "$exe" bench "${HASHTABLE_REPEATS:-9}" > "$SIXDB_RESULTS/migration.csv"
python3 -B workbench/spikes/hashtable/migration-report.py "$SIXDB_RESULTS/migration.csv" \
  --output "$SIXDB_RESULTS/summary.json" > "$SIXDB_RESULTS/summary.txt"
