#!/bin/bash
set -euo pipefail
cmake -S . -B build/spikes/memory-descriptors-virtual -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=memory-descriptors -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE=zen5 \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/spikes/memory-descriptors-virtual \
  --target memory_descriptors_virtual_growth -j "$SIXDB_BUILD_JOBS" \
  > "$SIXDB_RESULTS/build.log" 2>&1
descriptor_virtual=build/spikes/memory-descriptors-virtual/workbench/spikes/memory-descriptors/memory_descriptors_virtual_growth
taskset -c "$SIXDB_CPU" "$descriptor_virtual" --check > "$SIXDB_RESULTS/virtual-checks.txt"
taskset -c "$SIXDB_CPU" "$descriptor_virtual" --ms 20 --repetitions 3 > "$SIXDB_RESULTS/virtual-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_virtual" --ms 20 --repetitions 3 --reverse > "$SIXDB_RESULTS/virtual-reverse.csv"
python3 -B workbench/spikes/memory-descriptors/virtual-summary.py "$SIXDB_RESULTS" > "$SIXDB_RESULTS/summary-checks.txt"
lscpu > "$SIXDB_RESULTS/lscpu.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
