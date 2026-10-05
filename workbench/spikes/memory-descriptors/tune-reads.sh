#!/bin/bash
set -euo pipefail

descriptor_baseline="$SIXDB_SOURCE/build/spikes/memory-descriptors-frozen-v1"
python3 -B workbench/spikes/memory-descriptors/prepare-baseline.py "$descriptor_baseline" \
  > "$SIXDB_RESULTS/baseline-source.json"
cmake -S . -B build/spikes/memory-descriptors-tuned -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=memory-descriptors -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE=zen5 \
  -DSIXDB_DESCRIPTOR_BASELINE="$descriptor_baseline" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/spikes/memory-descriptors-tuned \
  --target memory_descriptors_grouped_bench memory_descriptors_frozen_bench \
  -j "$SIXDB_BUILD_JOBS" > "$SIXDB_RESULTS/build.log" 2>&1
descriptor_bin=build/spikes/memory-descriptors-tuned/workbench/spikes/memory-descriptors
for descriptor_order in forward reverse; do
  descriptor_flags=()
  descriptor_implementations=(frozen tuned)
  if [[ "$descriptor_order" == reverse ]]; then
    descriptor_flags=(--reverseorder)
    descriptor_implementations=(tuned frozen)
  fi
  for descriptor_implementation in "${descriptor_implementations[@]}"; do
    descriptor_executable=memory_descriptors_grouped_bench
    if [[ "$descriptor_implementation" == frozen ]]; then
      descriptor_executable=memory_descriptors_frozen_bench
    fi
    taskset -c "$SIXDB_CPU" "$descriptor_bin/$descriptor_executable" \
      --width 1024 --granule 4096 --layout class_major_reordered \
      --ms 100 --repetitions 5 --source-context "$SIXDB_JOB" \
      "${descriptor_flags[@]}" \
      > "$SIXDB_RESULTS/grouped-$descriptor_implementation-$descriptor_order.csv"
  done
done
lscpu > "$SIXDB_RESULTS/lscpu.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
