#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-growth-cycle
targets=()
if [[ ${HASHTABLE_ORACLE_PROBE:-0} == 1 ]]; then
  for case in 40_0_0 40_2_1 40_2_2 64_64_1 64_64_2; do
    for oracle in 0 1; do targets+=(hashtable_design_cycle_oracle_${case}_${oracle}); done
  done
else
for case in 40_0_0 40_2_1 40_2_2 64_64_1 64_64_2; do
  for slots in 14 28; do for step in 1 2; do
    targets+=(hashtable_design_cycle_${case}_${slots}_${step}_90)
  done; done
done
for case in 40_2_2 64_64_1; do for slots in 14 28; do for load in 80 100; do
  targets+=(hashtable_design_cycle_${case}_${slots}_1_${load})
done; done; done
for case in 8_0_0 16_0_0 24_3_1 32_4_1 48_16_2 56_32_1; do
  targets+=(hashtable_design_cycle_${case}_14_1_90)
done
fi
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir "$SIXDB_RESULTS/bin" "$SIXDB_RESULTS/codegen" "$SIXDB_RESULTS/panel"
for target in "${targets[@]}"; do
  cp "$build/workbench/spikes/hashtable/$target" "$SIXDB_RESULTS/bin/$target"
  llvm-objdump-21 -d --demangle "$SIXDB_RESULTS/bin/$target" | gzip > "$SIXDB_RESULTS/codegen/$target.s.gz"
  python3 -B workbench/spikes/hashtable/check_cycle_codegen.py "$SIXDB_RESULTS/codegen/$target.s.gz" \
    > "$SIXDB_RESULTS/codegen/$target.json"
  # The smallest boundaries verify no promotion for existing-key duplicates;
  # every timed large history also checks full contents and exact transitions.
  for n in 240 256 257; do
    if [[ $target == *_cycle_8_* && $n == 257 ]]; then continue; fi
    taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/bin/$target" check "$n" > "$SIXDB_RESULTS/$target-check$n.csv"
  done
done
for target in "${targets[@]}"; do
  populations=(3072 6144 7680)
  if [[ ${HASHTABLE_ORACLE_PROBE:-0} == 1 ]]; then populations=(6144);fi
  if [[ $target == *_cycle_8_* ]]; then populations=(240 256);fi
  for n in "${populations[@]}"; do for seed in 0 1 2; do
    stem=$target-n$n-s$seed
    /usr/bin/time -v -o "$SIXDB_RESULTS/panel/$stem-resources.txt" \
      env HT_CYCLE_TRACE="$SIXDB_RESULTS/panel/$stem-trace.csv" \
      taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/bin/$target" bench "$n" "${HASHTABLE_REPEATS:-5}" "$seed" \
      > "$SIXDB_RESULTS/panel/$stem.csv"
  done; done
done
python3 -B workbench/spikes/hashtable/growth-cycle-report.py "$SIXDB_RESULTS/panel" > "$SIXDB_RESULTS/qualified.txt"
