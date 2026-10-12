#!/bin/bash
set -euo pipefail
python3 - "$SIXDB_RESULTS" "$SIXDB_CPU" <<'PY'
import importlib.util, json, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location('hardware', 'workbench/spikes/memory-characterisation/hardware.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
Path(sys.argv[1], 'hardware.json').write_text(json.dumps(module.discover(int(sys.argv[2])), indent=2))
PY
case "$(uname -m)" in
  aarch64) variants=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) variants=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
for isa in "${variants[@]}"; do
  build="build/clang/streampack-scan-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_mutable_population streampack_geometry \
    streampack_model_check streampack_publication_check streampack_rewrite_check \
    streampack_widths_publication_check -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack"
  taskset -c "$SIXDB_CPU" "$bin/streampack_geometry" check > "$out/geometry-check.txt"
  taskset -c "$SIXDB_CPU" "$bin/streampack_widths_publication_check" > "$out/widths-publication-check.txt"
  for check in model publication rewrite; do
    taskset -c "$SIXDB_CPU" "$bin/streampack_${check}_check" > "$out/$check-check.txt"
  done
  # Exact occurrence counts for a byte common in stale initialized slack and
  # one uncommon there. Both are ordinary bytes in live binary values. Same
  # functions as checked histories; libc retains natural per-value execution.
  populations=(16384 4194304 16777216)
  if [[ $isa == x86-64-v4 ]]; then populations=(16384 4194304); fi
  for rows in "${populations[@]}"; do
    for sparse in 0 1; do
      for method in direct16 widths8; do
        key="$method-$rows-$sparse"
        taskset -c "$SIXDB_CPU" "$bin/streampack_mutable_population" "$method" "$rows" 256 "$sparse" 3 "$out/$key" scans > "$out/$key.csv"
      done
    done
  done
  # Same queries, block-amortized and globally scattered. Two process orders
  # pair the scalar and native selected-group reverse on identical width bytes.
  for trial in 0 1; do
    methods=(direct16 direct32 widths8 widths8-native-reverse)
    if [[ $trial == 1 ]]; then methods=(widths8-native-reverse widths8 direct32 direct16); fi
    for rows in "${populations[@]}"; do
      for method in "${methods[@]}"; do
        key="reverse-$trial-$method-$rows"
        taskset -c "$SIXDB_CPU" "$bin/streampack_mutable_population" "$method" "$rows" 256 1 2 "$out/$key" reverse > "$out/$key.csv"
      done
    done
  done
  cp "$bin/streampack_mutable_population" "$out/streampack_mutable_population"
  sha256sum "$out/streampack_mutable_population" > "$out/binary.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin/streampack_mutable_population" > "$out/streampack_mutable_population.asm"
done
