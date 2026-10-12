#!/bin/bash
set -euo pipefail
python3 workbench/spikes/streampack/references.py > "$SIXDB_RESULTS/references.txt"
python3 - "$SIXDB_RESULTS" "$SIXDB_CPU" <<'PY'
import importlib.util, json, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location('hardware', 'workbench/spikes/memory-characterisation/hardware.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
Path(sys.argv[1], 'hardware.json').write_text(json.dumps(module.discover(int(sys.argv[2])), indent=2))
PY
case "$(uname -m)" in
  x86_64) variants=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  aarch64) variants=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  *) exit 1 ;;
esac
for isa in "${variants[@]}"; do
  build="build/clang/streampack-prefix-$isa-$tune"
  out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out/footprint"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_footprint streampack_geometry \
    streampack_rewrite_check streampack_publication_check -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack"
  for check in rewrite publication; do
    taskset -c "$SIXDB_CPU" "$bin/streampack_${check}_check" > "$out/$check-check.txt"
  done
  taskset -c "$SIXDB_CPU" "$bin/streampack_geometry" check > "$out/geometry-check.txt"
  # A fresh process and construction per trial. Reverse method order in trial 2;
  # compare native vs scalar prefix with identical format and natural outputs.
  for trial in 0 1; do
    methods=(flat32 direct16 widths8 widths8-native location8 duckdb16)
    if [[ $trial == 1 ]]; then methods=(duckdb16 location8 widths8-native widths8 direct16 flat32); fi
    for n in 1024 262144 4194304 16777216; do
      for method in "${methods[@]}"; do
        key="$trial-$method-$n-256"
        taskset -c "$SIXDB_CPU" "$bin/streampack_footprint" "$method" "$n" 256 3 "$out/footprint/$key" > "$out/footprint/$key.csv"
      done
    done
  done
  taskset -c "$SIXDB_CPU" "$bin/streampack_geometry" pilot 5 > "$out/geometry.csv"
  for executable in streampack_footprint streampack_geometry; do
    cp "$bin/$executable" "$out/$executable"
    sha256sum "$out/$executable" >> "$out/binaries.sha256"
    llvm-objdump-21 --disassemble --demangle "$bin/$executable" > "$out/$executable.asm"
  done
done
