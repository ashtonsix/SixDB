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
[[ $(uname -m) == x86_64 ]]
tune=${STREAMPACK_TUNE:-zen5}
build="build/clang/streampack-slp-$tune"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log"
cmake --build "$build" --target streampack_footprint streampack_footprint_no_slp \
  -j "$SIXDB_BUILD_JOBS" > "$SIXDB_RESULTS/build.log" 2>&1
bin="$build/workbench/spikes/streampack"
for trial in 0 1; do
  variants=(streampack_footprint streampack_footprint_no_slp)
  if [[ $trial == 1 ]]; then variants=(streampack_footprint_no_slp streampack_footprint); fi
  for n in 1024 262144 4194304 16777216; do
    for exe in "${variants[@]}"; do
      for method in direct16 location8; do
        key="$trial-$exe-$method-$n"
        taskset -c "$SIXDB_CPU" "$bin/$exe" "$method" "$n" 256 3 "$SIXDB_RESULTS/$key" > "$SIXDB_RESULTS/$key.csv"
      done
    done
  done
done
for exe in streampack_footprint streampack_footprint_no_slp; do
  cp "$bin/$exe" "$SIXDB_RESULTS/$exe"
  sha256sum "$SIXDB_RESULTS/$exe" >> "$SIXDB_RESULTS/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin/$exe" > "$SIXDB_RESULTS/$exe.asm"
done
