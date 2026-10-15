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
build="build/hashtable-design-mutation-values-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
# Build the common linked dependencies once, and check the observer oracle on
# the original implementation before comparing the generated alternatives.
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target hashtable_design_value_2_bench \
    hashtable_design_mutation_patch_check -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
"$build/workbench/spikes/hashtable/hashtable_design_mutation_patch_check" > "$SIXDB_RESULTS/control-check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
/usr/bin/time -v -o "$SIXDB_RESULTS/probe-build-resources.txt" \
  python3 -B workbench/spikes/hashtable/mutation-probe.py "$build" "$SIXDB_RESULTS/probe" \
    --widths 2 8 16 --checks --check-widths 1 3 64 --check-variants partial \
    --extra-check hashtable_design_mutation_patch_check > "$SIXDB_RESULTS/probe-build.log" 2>&1
for width in 1 2 3 8 16 64; do
  "$SIXDB_RESULTS/probe/partial/values-$width-check" >> "$SIXDB_RESULTS/partial-check.txt"
done
"$SIXDB_RESULTS/probe/partial/hashtable_design_mutation_patch_check" >> "$SIXDB_RESULTS/partial-check.txt"
for variant in control fused partial; do
  for width in 2 8 16; do
    exe="$SIXDB_RESULTS/probe/$variant/values-$width"
    llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/probe/$variant/assembly-$width.gz"
    llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/probe/$variant/symbols-$width.txt"
  done
done
# Objects are reproducible from the command/source receipts; retain the actual
# timed ELFs and assembly instead of duplicating their debug data in objects.
find "$SIXDB_RESULTS/probe" -type f -name 'value-*.o' -delete
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" \
  python3 -B workbench/spikes/hashtable/mutation-run.py "$SIXDB_RESULTS/probe" "$SIXDB_RESULTS" \
    --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-5}"
python3 -B workbench/spikes/hashtable/mutation-report.py "$SIXDB_RESULTS/mutation.csv" \
  --output "$SIXDB_RESULTS/summary.json" > "$SIXDB_RESULTS/summary.txt"
