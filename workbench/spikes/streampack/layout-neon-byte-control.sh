#!/bin/bash
set -euo pipefail
phase=${1:-build}
[[ $(uname -m) == aarch64 && ( $phase == build || $phase == bench ) ]] || exit 1
build=build/clang/streampack-explicit-armv8.2-a+simd-neoverse-v3
out="$SIXDB_RESULTS/armv8.2-a+simd-neoverse-v3"
mkdir -p "$out"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE=neoverse-v3 > "$out/configure.log"
cmake --build "$build" --target streampack_layout_explicit -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack/streampack_layout_explicit"
"$bin" check > "$out/streampack_layout_explicit-checks.txt" 2>&1
cp "$bin" "$out/streampack_layout_explicit"
sha256sum "$bin" > "$out/binaries.sha256"
llvm-objdump-21 --disassemble --demangle "$bin" > "$out/streampack_layout_explicit.asm"
cp "$build/compile_commands.json" "$out/compile_commands.json"
[[ $phase == build ]] && exit 0
[[ ${STREAMPACK_REVIEWED_BINARY:-} == $(awk '{print $1}' "$out/binaries.sha256") ]] || exit 1
export STREAMPACK_LAYOUT_ORDERED_ONLY=1
for trial in 0 1; do
  for rows in 256 1048576 16777216; do
    for maximum in 255 4095; do
      placements=(planes hybrid-split32 hybrid32 hybrid128 block-planes block-hybrid)
      [[ $maximum == 255 ]] && placements+=(hybrid)
      if [[ $trial == 1 ]]; then
        reverse=()
        for ((i=${#placements[@]}-1; i>=0; --i)); do reverse+=("${placements[i]}"); done
        placements=("${reverse[@]}")
      fi
      for placement in "${placements[@]}"; do
        taskset -c "$SIXDB_CPU" "$bin" width "$placement" "$maximum" "$rows" 2 \
          > "$out/layout-$trial-width-$placement-$maximum-$rows.csv"
      done
    done
  done
done
