#!/bin/bash
# Usage: worker.py run workbench/spikes/frame-phase-2/seriespack/decode/worker.sh --machine neoverse-v2
set -euo pipefail
build=build/clang/frame-decode
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$SIXDB_RESULTS/configure.txt" 2>&1
cmake --build "$build" --target ikea_seriespack ikea_core -j "${SIXDB_BUILD_JOBS:-4}" > "$SIXDB_RESULTS/build.txt" 2>&1
clang++-21 -std=c++23 -O3 -march=armv8-a+simd -I ikea/include workbench/spikes/frame-phase-2/seriespack/decode/bench.cpp \
  $(find "$build" -name libikea_seriespack.a | head -1) $(find "$build" -name libikea_core.a | head -1) -o "$build/decode-bench" >> "$SIXDB_RESULTS/build.txt" 2>&1
for repeat in 1 2 3; do taskset -c "$SIXDB_CPU" "$build/decode-bench" >> "$SIXDB_RESULTS/decode.jsonl"; done
