#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  march=armv8-a+simd; tune=neoverse-v2
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
# These match the broad local mechanism controls. Fixed hierarchy geometry,
# including K8 and deliberately crowded wide cases, cannot select table policy.
cases=(8_0_0 64_0_0 16_2_1 16_2_2 40_2_1 40_2_2 40_64_1 40_64_2 64_2_2)
all_cases=(); targets=(); checks=()
suffixes=('' _compact)
if [[ ${HASHTABLE_PACKET_POLICY:-0} == 1 ]]; then suffixes=('' _hash1 _hash2); fi
for key in 8 16 24 32 40 48 56 64; do
  checks+=(hashtable_design_compact_hash_check_$key)
done
for case in "${cases[@]}"; do for suffix in "${suffixes[@]}"; do
  all_cases+=("$case$suffix")
  targets+=(hashtable_design_hierarchy_packet_$case$suffix)
done; done
build=build/hashtable-design-hierarchy-batch
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" "${checks[@]}" -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
bins=$build/workbench/spikes/hashtable
for check in "${checks[@]}"; do
  taskset -c "$SIXDB_CPU" "$bins/$check" > "$SIXDB_RESULTS/$check.txt"
done
for target in "${targets[@]}"; do
  taskset -c "$SIXDB_CPU" "$bins/$target" check > "$SIXDB_RESULTS/$target-check.txt"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$bins/$target" | gzip > "$SIXDB_RESULTS/$target.asm.gz"
  python3 -B workbench/spikes/hashtable/check_hierarchy_codegen.py "$SIXDB_RESULTS/$target.asm.gz" \
    > "$SIXDB_RESULTS/$target-codegen.json"
done
python3 -B workbench/spikes/hashtable/hierarchy-batch-run.py run "$bins" "$SIXDB_RESULTS/panel" \
  --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-9}" --cases "${all_cases[@]}" \
  --counts 64 192 768 > "$SIXDB_RESULTS/panel.txt"
