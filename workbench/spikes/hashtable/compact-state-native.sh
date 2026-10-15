#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  march=armv8-a+simd; tune=neoverse-v2
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then march=graniterapids; fi
fi
keys=(8 16 24 32 40 48 56 64);targets=()
for key in "${keys[@]}"; do targets+=(hashtable_design_compact_state_$key); done
build=build/hashtable-design-compact-state
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir "$SIXDB_RESULTS/bin"
for target in "${targets[@]}"; do
  exe=$build/workbench/spikes/hashtable/$target
  cp "$exe" "$SIXDB_RESULTS/bin/$target"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/$target-check.txt"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/$target.asm.gz"
done
for order in 0 1; do
  if ((order)); then keys=(64 56 48 40 32 24 16 8); fi
  for key in "${keys[@]}"; do
    taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/bin/hashtable_design_compact_state_$key" \
      bench "${HASHTABLE_REPEATS:-9}" > "$SIXDB_RESULTS/state-$key-order$order.csv"
  done
done
