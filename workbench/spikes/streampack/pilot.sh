#!/bin/bash
set -euo pipefail
# Ready-operation discovery; no end-to-end lifetime or cache-level claim.
python3 workbench/spikes/streampack/references.py > "$SIXDB_RESULTS/reference.txt"
case "$(uname -m)" in
  x86_64) variants=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  aarch64) variants=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v2} ;;
  *) exit 1 ;;
esac
for isa in "${variants[@]}"; do
  build="build/clang/streampack-pilot-$isa-$tune"
  out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=streampack -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_model_check streampack_publication_check streampack_pilot -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/streampack/streampack_model_check" > "$out/check.log"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/streampack/streampack_publication_check" > "$out/publication-check.log"
  executable="$build/workbench/spikes/streampack/streampack_pilot"
  taskset -c "$SIXDB_CPU" "$executable" all 4096 5 > "$out/pilot.csv"
  llvm-objdump-21 --disassemble --demangle "$executable" > "$out/pilot.asm"
  sha256sum "$executable" > "$out/binary.sha256"
  ldd "$executable" > "$out/libraries.txt"
  # Statistical process profile includes calibration/setup; not exact counters
  # for a composed interval. Keep permission failures as diagnostics.
  if command -v perf >/dev/null; then
    if perf record -e cpu-clock:u -F 997 -o "$out/profile.data" -- \
        taskset -c "$SIXDB_CPU" "$executable" search 4096 1 > "$out/profile.csv" 2> "$out/profile.log"; then
      perf report --stdio -i "$out/profile.data" > "$out/profile.txt" 2>&1
    fi
  fi
done
