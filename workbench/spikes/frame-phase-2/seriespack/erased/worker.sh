#!/bin/bash
# Usage: worker.py run workbench/spikes/frame-phase-2/seriespack/erased/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
case "$(uname -m)" in
  x86_64) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5"; march=znver5 ;;
  *) flags=""; march=armv8-a+simd ;;
esac
build=build/clang/frame-erased
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release $flags > "$SIXDB_RESULTS/configure.txt" 2>&1
/usr/bin/time -v cmake --build "$build" --target ikea_seriespack ikea_core -j "${SIXDB_BUILD_JOBS:-4}" > "$SIXDB_RESULTS/build.txt" 2>&1
clang++-21 -std=c++23 -O3 -march=$march -I ikea/include workbench/spikes/frame-phase-2/seriespack/erased/bench.cpp \
  $(find "$build" -name libikea_seriespack.a | head -1) $(find "$build" -name libikea_core.a | head -1) -o "$build/erased-bench" >> "$SIXDB_RESULTS/build.txt" 2>&1
for repeat in 1 2 3; do taskset -c "$SIXDB_CPU" "$build/erased-bench" | sed "s/^{/{\"repeat\":$repeat,/" >> "$SIXDB_RESULTS/erased.jsonl"; done
