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
build="build/hashtable-design-selector-mix-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
checks=(hashtable_design_selector_hash_mask_check hashtable_design_selector_hash_packet_check
  hashtable_design_selector_values_check)
targets=("${checks[@]}")
for case in 16_2_1 40_0_0 40_2_1 40_8_2 64_16_1; do
  for variant in fold multiply; do targets+=(hashtable_design_selector_mix_${case}_${variant}); done
done
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target "${targets[@]}" -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_selector_(hash_mask|hash_packet|values)_check$' \
  --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for check in "${checks[@]}"; do
  cp "$build/workbench/spikes/hashtable/$check" "$SIXDB_RESULTS/$check"
done
llvm-objdump-21 -d --demangle --no-show-raw-insn \
  "$SIXDB_RESULTS/hashtable_design_selector_hash_packet_check" | gzip > "$SIXDB_RESULTS/hash-packet-assembly.gz"
for case in 16_2_1 40_0_0 40_2_1 40_8_2 64_16_1; do
  for variant in fold multiply; do
    exe="$build/workbench/spikes/hashtable/hashtable_design_selector_mix_${case}_${variant}"
    cp "$exe" "$SIXDB_RESULTS/selector-mix-$case-$variant"
    llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/assembly-$case-$variant.gz"
  done
done
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" \
  python3 -B workbench/spikes/hashtable/occupancy-run.py run "$SIXDB_RESULTS" "$SIXDB_RESULTS" \
    --family selector-mix --cpu "$SIXDB_CPU" --passes 2 --reps "${HASHTABLE_REPEATS:-5}" > "$SIXDB_RESULTS/summary.txt"
