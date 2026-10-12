#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isa=armv8.2-a+simd; tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isa=x86-64-v3; tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
export STREAMPACK_LAYOUT_BULK_ONLY=1
build="build/clang/streampack-layout-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
mkdir -p "$out"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
cmake --build "$build" --target streampack_layout_fields streampack_layout_growth -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack/streampack_layout_fields"
"$bin" check > "$out/checks.txt" 2>&1
for trial in 0 1; do
  placements=(planes series tuple64 hybrid32 block-planes block-hybrid)
  [[ $trial == 1 ]] && placements=(block-hybrid block-planes hybrid32 tuple64 series planes)
  for rows in 1024 1048576 16777216; do
    for placement in "${placements[@]}"; do
      taskset -c "$SIXDB_CPU" "$bin" width "$placement" 4095 "$rows" 2 > "$out/layout-$trial-width-$placement-4095-$rows.csv"
    done
  done
done
growth="$build/workbench/spikes/streampack/streampack_layout_growth"
"$growth" check > "$out/growth-checks.txt" 2>&1
taskset -c "$SIXDB_CPU" "$growth" > "$out/growth-0.csv"
taskset -c "$SIXDB_CPU" "$growth" reverse > "$out/growth-1.csv"
cp "$growth" "$out/streampack_layout_growth"
cp "$bin" "$out/streampack_layout_fields"
sha256sum "$bin" > "$out/binaries.sha256"
llvm-objdump-21 --disassemble --demangle "$bin" > "$out/streampack_layout_fields.asm"
