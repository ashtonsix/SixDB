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
build=build/hashtable-design-vectors
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_design_vector_check hashtable_design_vector_publication \
  hashtable_design_vector_reserved_check hashtable_design_vectors_reserved_8 hashtable_design_vectors_reserved_64 \
  hashtable_design_vectors_2 hashtable_design_vectors_8 hashtable_design_vectors_32 hashtable_design_vectors_64 \
  -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_vector_(check|publication|reserved_check)$' --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for w in 2 8 32 64; do
  exe=hashtable_design_vectors_$w
  cp "$build/workbench/spikes/hashtable/$exe" "$SIXDB_RESULTS/$exe"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$SIXDB_RESULTS/$exe" | gzip > "$SIXDB_RESULTS/$exe.asm.gz"
  llvm-nm-21 -S -C "$SIXDB_RESULTS/$exe" > "$SIXDB_RESULTS/$exe.symbols"
done
for w in 8 64; do
  exe=hashtable_design_vectors_reserved_$w
  cp "$build/workbench/spikes/hashtable/$exe" "$SIXDB_RESULTS/$exe"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$SIXDB_RESULTS/$exe" | gzip > "$SIXDB_RESULTS/$exe.asm.gz"
  llvm-nm-21 -S -C "$SIXDB_RESULTS/$exe" > "$SIXDB_RESULTS/$exe.symbols"
done
extra=(); reserved_extra=()
if [[ ${HASHTABLE_VECTOR_BOUNDARIES:-0} == 1 ]]; then
  # The default inline-capacity panel does not include every external-vector
  # doubling boundary. Keep this targeted follow-up separate from that cohort.
  extra=(--widths 8 32 64 --fanouts 4 5 8 9)
  reserved_extra=(--fanouts 4 5 8 9)
fi
python3 workbench/spikes/hashtable/vector-run.py "$SIXDB_RESULTS" "$SIXDB_RESULTS/panel" \
  --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-3}" "${extra[@]}" > "$SIXDB_RESULTS/panel.txt"
python3 workbench/spikes/hashtable/vector-run.py "$SIXDB_RESULTS" "$SIXDB_RESULTS/reserved" \
  --cpu "$SIXDB_CPU" --reps "${HASHTABLE_REPEATS:-3}" --widths 8 64 \
  --prefix hashtable_design_vectors_reserved "${reserved_extra[@]}" > "$SIXDB_RESULTS/reserved.txt"
