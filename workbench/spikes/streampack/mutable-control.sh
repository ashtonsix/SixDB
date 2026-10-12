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
  build="build/clang/streampack-mutable-$isa-$tune"
  out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out/mutable"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_mutable_population streampack_geometry \
    streampack_lifecycle -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack"
  taskset -c "$SIXDB_CPU" "$bin/streampack_geometry" check > "$out/geometry-check.txt"
  for rows in 16384 4194304; do
    capacities=(128 256 512); populations=(0 1)
    # The v4 control asks about code generation on the same sparse N256 trace;
    # the complete capacity/population sweep uses the primary v3 build.
    if [[ $isa == x86-64-v4 ]]; then capacities=(256); populations=(1); fi
    for cap in "${capacities[@]}"; do
      for sparse in "${populations[@]}"; do
        for method in direct16 widths8 direct32; do
          key="$method-$rows-$cap-$sparse"
          taskset -c "$SIXDB_CPU" "$bin/streampack_mutable_population" "$method" "$rows" "$cap" "$sparse" 3 "$out/mutable/$key" > "$out/mutable/$key.csv"
        done
      done
    done
  done
  if [[ $isa != x86-64-v4 ]]; then
    for method in direct32 widths8 direct16; do
      key="$method-16777216-256-1"
      taskset -c "$SIXDB_CPU" "$bin/streampack_mutable_population" "$method" 16777216 256 1 2 "$out/mutable/$key" > "$out/mutable/$key.csv"
    done
    run_lifecycle() {
      local label=$1; shift
      local status=0
      taskset -c "$SIXDB_CPU" "$bin/streampack_lifecycle" "$@" > "$out/$label.csv" 2> "$out/$label.txt" || status=$?
      printf '%s,%s\n' "$label" "$status" >> "$out/outcomes.csv"
      [[ $status == 0 || $status == 2 ]]
    }
    for rep in 0 1 2; do
      for cap in 128 256 512; do
        objects=$((1048576 / cap))
        run_lifecycle "$rep-N$cap-pressure" direct stable "$objects" "$cap" 48 0 4 1024
      done
      for phase in 0 7; do
        run_lifecycle "$rep-N256-cadence8-phase$phase" direct stable 4096 256 48 8 4 1024 "$phase"
      done
    done
    # Expensive phase accounting is separate from timing, and explicitly tests
    # both alignments against the fixed two-epoch reader-retirement schedule.
    for phase in 0 7; do
      STREAMPACK_ACCOUNTING=1 run_lifecycle "accounting-cadence8-phase$phase" direct stable 4096 256 48 8 4 1024 "$phase"
      run_lifecycle "tight832-cadence8-phase$phase" direct stable 4096 256 48 8 4 832 "$phase"
    done
    run_lifecycle "tight832-pressure" direct stable 4096 256 48 0 4 832
    if command -v perf >/dev/null; then
      if perf record -e cpu-clock:u -F 997 -o "$out/profile.data" -- taskset -c "$SIXDB_CPU" \
          "$bin/streampack_lifecycle" direct stable 4096 256 48 0 4 1024 > "$out/profile.csv" 2> "$out/profile.txt"; then
        perf report --stdio -i "$out/profile.data" > "$out/profile-report.txt" 2>&1
      fi
    fi
  fi
  for executable in streampack_mutable_population streampack_lifecycle; do
    cp "$bin/$executable" "$out/$executable"
    sha256sum "$out/$executable" >> "$out/binaries.sha256"
    llvm-objdump-21 --disassemble --demangle "$bin/$executable" > "$out/$executable.asm"
  done
done
