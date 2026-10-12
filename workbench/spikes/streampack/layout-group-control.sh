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
  cmake --build "$build" --target streampack_layout -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  bin="$build/workbench/spikes/streampack/streampack_layout"
  "$bin" check > "$out/checks.txt" 2>&1
  for trial in 0 1; do
    geometries=(direct width); placements=(planes group-tight group96 group128 series tuple tuple-tight tuple64)
    if [[ $trial == 1 ]]; then geometries=(width direct); placements=(tuple64 tuple-tight tuple series group128 group96 group-tight planes); fi
    populations=(1024 1048576 16777216)
    if [[ $isa == x86-64-v4 ]]; then populations=(1048576); fi
    for rows in "${populations[@]}"; do
      for geometry in "${geometries[@]}"; do
        for placement in "${placements[@]}"; do
          [[ $geometry == direct && $placement != planes ]] && continue
          key="layout-$trial-$geometry-$placement-4095-$rows"
          taskset -c "$SIXDB_CPU" "$bin" "$geometry" "$placement" 4095 "$rows" 2 > "$out/$key.csv"
        done
      done
    done
  done
  cp "$bin" "$out/streampack_layout"
  sha256sum "$bin" > "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin" > "$out/streampack_layout.asm"
done
