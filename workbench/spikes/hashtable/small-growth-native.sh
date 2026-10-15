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
read -ra keys <<< "${HASHTABLE_SMALL_KEYS:-8 16 24 32 40 48 56 64}"
targets=(hashtable_design_list_growth_check)
for key in "${keys[@]}"; do for grow in 0 1; do
  targets+=(hashtable_design_small_batch_${key}_grow${grow})
  if [[ ${HASHTABLE_FULL_OWNER_CONTROL:-0} == 1 ]]; then
    targets+=(hashtable_design_small_batch_${key}_grow${grow}_full)
  fi
done; done
build=build/hashtable-design-small-growth
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir "$SIXDB_RESULTS/bin" "$SIXDB_RESULTS/codegen"
for target in "${targets[@]}"; do
  exe=$build/workbench/spikes/hashtable/$target
  cp "$exe" "$SIXDB_RESULTS/bin/$target"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/$target-check.txt"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/codegen/$target.s.gz"
done
extra=()
if [[ ${HASHTABLE_FULL_OWNER_CONTROL:-0} == 1 ]]; then extra+=(--full-owner-control); fi
python3 -B workbench/spikes/hashtable/small-growth-run.py "$SIXDB_RESULTS/bin" "$SIXDB_RESULTS/panel" \
  --cpu "$SIXDB_CPU" --keys "${keys[@]}" --reps "${HASHTABLE_REPEATS:-7}" \
  --histories "${HASHTABLE_HISTORIES:-64}" "${extra[@]}" > "$SIXDB_RESULTS/panel.txt"
python3 -B - "$SIXDB_RESULTS" <<'PY'
import importlib.util,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('surface_report',Path('workbench/spikes/hashtable/surface-report.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.codegen(Path(sys.argv[1]))
PY
