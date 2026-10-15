#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2;
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-coupled
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cases=(${HT_COUPLED_CASES:-16_2_1 40_16_1 64_16_1 40_8_2});targets=();prefix=hashtable_design_coupled_;options=()
if [[ ${HT_CAPACITY_FAMILIES:-0} == 1 ]]; then
  prefix=hashtable_design_capacity_
  cases=(${HT_COUPLED_CASES:-16_0_0 24_3_1 32_4_1 40_0_0 40_2_1 48_16_2 56_32_1 64_16_1 64_64_1 40_2_2 64_64_2})
  options=(--capacity-families)
fi
for case in "${cases[@]}"; do targets+=($prefix$case); done
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for target in "${targets[@]}"; do
  cp "$build/workbench/spikes/hashtable/$target" "$SIXDB_RESULTS/$target"
  llvm-objdump-21 -d --demangle "$SIXDB_RESULTS/$target" | gzip > "$SIXDB_RESULTS/$target.asm.gz"
done
python3 -B workbench/spikes/hashtable/coupled-run.py "$SIXDB_RESULTS" "$SIXDB_RESULTS/panel" \
  --cases "${cases[@]}" --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-3}" "${options[@]}" > "$SIXDB_RESULTS/panel.txt"
if [[ ${HT_CAPACITY_FAMILIES:-0} == 1 ]]; then
  python3 -B workbench/spikes/hashtable/capacity-report.py "$SIXDB_RESULTS/panel" \
    --output "$SIXDB_RESULTS/qualified" --reps "${HASHTABLE_REPEATS:-3}" > "$SIXDB_RESULTS/qualified.txt"
fi
