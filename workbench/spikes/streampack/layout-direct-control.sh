#!/bin/bash
set -euo pipefail
phase=${1:-build}
[[ $phase == build || $phase == bench ]] || exit 1
case "$(uname -m)" in
  aarch64) isas=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isas=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
for isa in "${isas[@]}"; do
  build="build/clang/streampack-direct-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_layout_direct -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack/streampack_layout_direct"
  "$bin" check > "$out/checks.txt" 2>&1
  cp "$bin" "$out/streampack_layout_direct"
  sha256sum "$bin" > "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin" > "$out/streampack_layout_direct.asm"
  cp "$build/compile_commands.json" "$out/compile_commands.json"
  [[ $phase == build ]] && continue
  key=${isa//[^a-zA-Z0-9]/_}; key="STREAMPACK_REVIEWED_$key"
  actual=$(awk '{print $1}' "$out/binaries.sha256")
  [[ ${!key:-} == "$actual" ]] || { echo "Unreviewed Direct binary: $isa $actual" >&2; exit 1; }
  for trial in 0 1; do
    for rows in 256 1048576 16777216; do
      for maximum in 255 4095; do
        placements=(planes block-planes group-tight group128 series tuple)
        [[ $maximum == 255 ]] && placements=(planes block-planes group-tight group64 group96 group128 series tuple)
        if [[ $trial == 1 ]]; then
          reverse=(); for ((i=${#placements[@]}-1; i>=0; --i)); do reverse+=("${placements[i]}"); done
          placements=("${reverse[@]}")
        fi
        for placement in "${placements[@]}"; do
          taskset -c "$SIXDB_CPU" "$bin" direct "$placement" "$maximum" "$rows" 2 \
            > "$out/layout-$trial-direct-$placement-$maximum-$rows.csv"
        done
      done
    done
  done
done
