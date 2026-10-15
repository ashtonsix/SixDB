#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
lscpu -e=CPU,CORE,SOCKET,NODE > "$SIXDB_RESULTS/topology.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
march=${HASHTABLE_MARCH:-$march}
concurrent=${HASHTABLE_CONCURRENT_WORKERS:-OFF}
target=hashtable_design_workers
if [[ $concurrent == ON ]]; then target=hashtable_design_concurrent_workers; fi
build="build/hashtable-design-workers-$isa-$concurrent"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  -DSIXDB_HASHTABLE_CONCURRENT_WORKERS="$concurrent" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "$target" hashtable_design_compare -j "${SIXDB_BUILD_JOBS:-1}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
base="$build/workbench/spikes/hashtable"
exe="$base/$target"
"$base/hashtable_design_compare" check > "$SIXDB_RESULTS/check.txt"
"$exe" check > "$SIXDB_RESULTS/workers-check.csv"
cp "$exe" "$SIXDB_RESULTS/hashtable_design_workers"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
llvm-objdump-21 -d --demangle "$exe" | gzip > "$SIXDB_RESULTS/assembly.gz"
python3 workbench/spikes/hashtable/check_workers_codegen.py "$SIXDB_RESULTS/assembly.gz" \
  > "$SIXDB_RESULTS/codegen.json"
for mode in hot large; do
  if [[ $mode == hot ]]; then n=1800; else n=110000; fi
  /usr/bin/time -v -o "$SIXDB_RESULTS/$mode-resources.txt" \
    "$exe" "$mode" "${HASHTABLE_REPEATS:-9}" > "$SIXDB_RESULTS/$mode.csv"
  python3 workbench/spikes/hashtable/workers-report.py "$SIXDB_RESULTS/$mode.csv" --n "$n" \
    --output "$SIXDB_RESULTS/$mode-summary.json" > "$SIXDB_RESULTS/$mode-summary.txt"
done
