#!/bin/bash
# Patched point reads over the bounds stage's representation, on phase 1's columns.
# Usage: worker.py run workbench/spikes/frame-phase-2/seriespack/patch/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
# Inputs go to the worker's shared data cache, never into collected results.
inputs=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
cp "$inputs" "$SIXDB_RESULTS/input-artifact.json"
input_root=$(python3 workbench/tools/artifacts.py input "$inputs" 2> "$SIXDB_RESULTS/input-fetch.txt")
cols="$input_root/inputs/cols"
case "$(uname -m)" in
  x86_64) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5"; march=znver5 ;;
  *) flags=""; march=armv8-a+simd ;;
esac
build=build/clang/frame-patch
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release $flags > "$SIXDB_RESULTS/configure.txt" 2>&1
cmake --build "$build" --target ikea_seriespack ikea_core -j "${SIXDB_BUILD_JOBS:-4}" > "$SIXDB_RESULTS/build.txt" 2>&1
clang++-21 -std=c++23 -O3 -march=$march -I ikea/include workbench/spikes/frame-phase-2/seriespack/patch/points.cpp \
  $(find "$build" -name libikea_seriespack.a | head -1) $(find "$build" -name libikea_core.a | head -1) -o "$build/patch-points" >> "$SIXDB_RESULTS/build.txt" 2>&1
for repeat in 1 2 3; do
  taskset -c "$SIXDB_CPU" "$build/patch-points" "$cols" 2999808 5 ClientIP OS ResolutionWidth UserID RegionID URLHash \
    | sed "s/^{/{\"repeat\":$repeat,/" >> "$SIXDB_RESULTS/points.jsonl"
done
