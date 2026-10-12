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
  build="build/clang/streampack-primitive-$isa-$tune"
  out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out/footprint"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_model_check streampack_publication_check streampack_substring_check \
    streampack_geometry streampack_footprint streampack_pilot -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack"
  for check in model publication substring; do
    taskset -c "$SIXDB_CPU" "$bin/streampack_${check}_check" > "$out/$check-check.txt"
  done
  taskset -c "$SIXDB_CPU" "$bin/streampack_geometry" check > "$out/geometry-check.txt"
  # Fresh process per method and population; sequential repetitions, full rank
  # domain accessed, no other candidate or request/oracle arrays left resident.
  for n in 1024 16384 262144 4194304 16777216; do
    for method in flat32 direct16 widths8 location8 duckdb16; do
      key="$method-$n-256"
      taskset -c "$SIXDB_CPU" "$bin/streampack_footprint" "$method" "$n" 256 3 "$out/footprint/$key" > "$out/footprint/$key.csv"
    done
  done
  # Separately vary block capacity without changing the logical population.
  for n in 262144 4194304; do
    for cap in 128 512; do
      key="direct16-$n-$cap"
      taskset -c "$SIXDB_CPU" "$bin/streampack_footprint" direct16 "$n" "$cap" 3 "$out/footprint/$key" > "$out/footprint/$key.csv"
    done
  done
  taskset -c "$SIXDB_CPU" "$bin/streampack_geometry" pilot 5 > "$out/geometry.csv"
  taskset -c "$SIXDB_CPU" "$bin/streampack_pilot" all 4096 5 > "$out/ready.csv"
  for executable in streampack_geometry streampack_footprint streampack_pilot; do
    cp "$bin/$executable" "$out/$executable"
    sha256sum "$out/$executable" >> "$out/binaries.sha256"
    llvm-objdump-21 --disassemble --demangle "$bin/$executable" > "$out/$executable.asm"
  done
  ldd "$bin/streampack_footprint" > "$out/libraries.txt"
  if command -v perf >/dev/null; then
    if perf record -e cpu-clock:u -F 997 -o "$out/profile.data" -- taskset -c "$SIXDB_CPU" \
        "$bin/streampack_geometry" pilot 1 > "$out/profile.csv" 2> "$out/profile.log"; then
      perf report --stdio -i "$out/profile.data" > "$out/profile.txt" 2>&1
    fi
  fi
done
