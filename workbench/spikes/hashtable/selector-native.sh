#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
[[ $(uname -m) == x86_64 ]]
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
if [[ $isa == avx512 ]]; then
  if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
fi
build="build/hashtable-design-selector-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
targets=(hashtable_design_recipe_view_check hashtable_design_recipe_scan32_check)
for key in 16 40; do
  for variant in composed16 direct16 composed32 direct32; do
    targets+=(hashtable_design_selector_${key}_2_${variant})
  done
done
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target "${targets[@]}" -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_recipe_(view|scan32)_check$' \
  --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for key in 16 40; do
  for variant in composed16 direct16 composed32 direct32; do
    exe="$build/workbench/spikes/hashtable/hashtable_design_selector_${key}_2_${variant}"
    cp "$exe" "$SIXDB_RESULTS/selector-$variant-$key-2"
    llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/assembly-$variant-$key-2.gz"
    llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/symbols-$variant-$key-2.txt"
  done
done
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" \
  python3 -B workbench/spikes/hashtable/geometry-run.py "$SIXDB_RESULTS" "$SIXDB_RESULTS" \
    --cpu "$SIXDB_CPU" --keys 16 40 --widths 2 --selector-variants --passes 2 --reps "${HASHTABLE_REPEATS:-5}"
python3 -B workbench/spikes/hashtable/geometry-report.py "$SIXDB_RESULTS" \
  --output "$SIXDB_RESULTS/summary.json" --verify-binaries > "$SIXDB_RESULTS/summary.txt"
