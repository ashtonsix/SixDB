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
build="build/hashtable-design-values-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
targets=()
for width in 1 2 3 4 7 8 9 16 17 32 64; do targets+=(hashtable_design_value_${width}_check); done
for width in 2 8 16; do targets+=(hashtable_design_value_${width}_bench); done
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target "${targets[@]}" -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_value_[0-9]+_check$' --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for width in 2 8 16; do
  exe="$build/workbench/spikes/hashtable/hashtable_design_value_${width}_bench"
  cp "$exe" "$SIXDB_RESULTS/values-$width"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/assembly-$width.gz"
  llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/symbols-$width.txt"
done
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" \
  python3 -B workbench/spikes/hashtable/values-run.py "$SIXDB_RESULTS" "$SIXDB_RESULTS" --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-9}"
