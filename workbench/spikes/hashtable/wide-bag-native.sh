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
build="build/hashtable-design-wide-bags-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
targets=()
for key in 16 40 64; do
  for width in 32 64; do targets+=(hashtable_design_wide_bag_${key}_${width}); done
done
for key in 8 16 24 32 40 48 56 64; do targets+=(hashtable_design_wide_inline_64_${key}_check); done
for width in 1 32 57 63; do targets+=(hashtable_design_wide_inline_${width}_40_check); done
for width in 32 64; do
  targets+=(hashtable_design_wide_bag_40_${width}_zero hashtable_design_wide_inline_${width}_64_zero_check)
done
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_wide_inline_[0-9]+_[0-9]+(_zero)?_check$' --output-on-failure \
  > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
bin="$build/workbench/spikes/hashtable"
# Preserve native symbols and assembly independently of the measurement loop.
for key in 16 40 64; do
  for width in 32 64; do
    name=hashtable_design_wide_bag_${key}_${width}
    llvm-objdump-21 -d --demangle --no-show-raw-insn "$bin/$name" | gzip > "$SIXDB_RESULTS/$name.asm.gz"
    llvm-nm-21 -S -C "$bin/$name" > "$SIXDB_RESULTS/$name.symbols"
  done
done
for width in 32 64; do
  name=hashtable_design_wide_bag_40_${width}_zero
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$bin/$name" | gzip > "$SIXDB_RESULTS/$name.asm.gz"
  llvm-nm-21 -S -C "$bin/$name" > "$SIXDB_RESULTS/$name.symbols"
done
/usr/bin/time -v -o "$SIXDB_RESULTS/surface-resources.txt" \
  python3 -B workbench/spikes/hashtable/wide-bag-run.py run "$bin" "$SIXDB_RESULTS/surface" \
    --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-5}" --extended > "$SIXDB_RESULTS/surface-report.txt"
/usr/bin/time -v -o "$SIXDB_RESULTS/pair-resources.txt" \
  python3 -B workbench/spikes/hashtable/wide-bag-run.py pair "$bin" "$SIXDB_RESULTS/zero-pair" \
    --control "$SIXDB_RESULTS/surface" --cpu "$SIXDB_CPU" --cases 40_32 40_64 \
    --arms 0 2 8 9 10 11 12 --reps "${HASHTABLE_REPEATS:-5}" --extended > "$SIXDB_RESULTS/pair-report.txt"
