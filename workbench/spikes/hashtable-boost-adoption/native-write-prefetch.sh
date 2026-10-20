#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-boost-adoption
build="$SIXDB_SOURCE/build/hashtable-memory-native"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target memory_ikea_40_22_0 memory_ikea_40_21_8 \
  memory_ikea_50_22_0 memory_ikea_50_21_8 memory_peers_40_0 memory_peers_40_8 \
  memory_peers_50_0 memory_peers_50_8 -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir -p "$SIXDB_RESULTS/binaries"
cp "$build/$study"/memory_ikea_{40_22_0,40_21_8,50_22_0,50_21_8} \
  "$build/$study"/memory_peers_{40_0,40_8,50_0,50_8} "$SIXDB_RESULTS/binaries/"
python3 "$study/write-prefetch-run.py" --binaries "$SIXDB_RESULTS/binaries" \
  --output "$SIXDB_RESULTS/native" --cpu "$SIXDB_CPU"
