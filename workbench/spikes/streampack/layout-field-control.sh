#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isas=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isas=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
export STREAMPACK_LAYOUT_REVERSE=1
for isa in "${isas[@]}"; do
  build="build/clang/streampack-layout-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_layout_fields streampack_layout_growth -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack/streampack_layout_fields"
  "$bin" check > "$out/checks.txt" 2>&1
  for trial in 0 1; do
    placements=(planes group-tight series tuple64 hybrid-split32 hybrid32 hybrid hybrid128)
    [[ $trial == 1 ]] && placements=(hybrid128 hybrid hybrid32 hybrid-split32 tuple64 series group-tight planes)
    populations=(1024 1048576 16777216)
    [[ $isa == x86-64-v4 ]] && populations=(1048576)
    for rows in "${populations[@]}"; do
      for maximum in 255 4095; do
        for placement in "${placements[@]}"; do
          [[ $maximum == 4095 && $placement == hybrid ]] && continue
          key="layout-$trial-width-$placement-$maximum-$rows"
          taskset -c "$SIXDB_CPU" "$bin" width "$placement" "$maximum" "$rows" 2 > "$out/$key.csv"
        done
      done
    done
  done
  if [[ $isa == "${isas[0]}" ]]; then
    growth="$build/workbench/spikes/streampack/streampack_layout_growth"
    "$growth" check > "$out/growth-checks.txt" 2>&1
    taskset -c "$SIXDB_CPU" "$growth" > "$out/growth-0.csv"
    taskset -c "$SIXDB_CPU" "$growth" reverse > "$out/growth-1.csv"
    cp "$growth" "$out/streampack_layout_growth"
  fi
  cp "$bin" "$out/streampack_layout_fields"
  sha256sum "$bin" > "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin" > "$out/streampack_layout_fields.asm"
done
