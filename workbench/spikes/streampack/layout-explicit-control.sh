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
  build="build/clang/streampack-explicit-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_layout_explicit streampack_layout_explicit_bulk -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  for target in streampack_layout_explicit streampack_layout_explicit_bulk; do
    bin="$build/workbench/spikes/streampack/$target"
    "$bin" check > "$out/$target-checks.txt" 2>&1
    cp "$bin" "$out/$target"
    sha256sum "$bin" >> "$out/binaries.sha256"
    llvm-objdump-21 --disassemble --demangle "$bin" > "$out/$target.asm"
  done
  cmake --build "$build" --target help > /dev/null
  cp "$build/compile_commands.json" "$out/compile_commands.json"
  [[ $phase == build ]] && continue
  # Reruns must execute the exact pair inspected after the build-only job.
  key=${isa//[^a-zA-Z0-9]/_}; key="STREAMPACK_REVIEWED_$key"
  actual=$(awk '{print $1}' "$out/binaries.sha256" | paste -sd,)
  [[ ${!key:-} == "$actual" ]] || { echo "Unreviewed binary pair: $isa $actual" >&2; exit 1; }
  export STREAMPACK_LAYOUT_BULK_ONLY=1
  for trial in 0 1; do
    placements=(planes hybrid-split32 hybrid32 hybrid128 block-planes block-hybrid)
    bulk=(planes series tuple64 hybrid32 block-planes block-hybrid)
    if [[ $trial == 1 ]]; then
      placements=(block-hybrid block-planes hybrid128 hybrid32 hybrid-split32 planes)
      bulk=(block-hybrid block-planes hybrid32 tuple64 series planes)
    fi
    for rows in 256 1048576 16777216; do
      for maximum in 255 4095; do
        methods=("${placements[@]}")
        [[ $maximum == 255 ]] && methods+=(hybrid)
        for placement in "${methods[@]}"; do
          taskset -c "$SIXDB_CPU" "$build/workbench/spikes/streampack/streampack_layout_explicit" \
            width "$placement" "$maximum" "$rows" 2 > "$out/layout-$trial-width-$placement-$maximum-$rows.csv"
        done
      done
      for placement in "${bulk[@]}"; do
        taskset -c "$SIXDB_CPU" "$build/workbench/spikes/streampack/streampack_layout_explicit_bulk" \
          width "$placement" 4095 "$rows" 2 > "$out/bulk-$trial-width-$placement-4095-$rows.csv"
      done
    done
  done
done
