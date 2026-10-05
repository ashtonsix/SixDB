#!/bin/bash
set -euo pipefail
cmake -S . -B build/spikes/object-composition -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=orbital-objects -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE=zen5 \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/spikes/object-composition \
  --target orbital_objects_composition_probe orbital_objects_composition_bench \
  orbital_objects_views orbital_objects_capabilities -j "$SIXDB_BUILD_JOBS" \
  > "$SIXDB_RESULTS/build.log" 2>&1
object_bin=build/spikes/object-composition/workbench/spikes/orbital-objects
taskset -c "$SIXDB_CPU" "$object_bin/orbital_objects_composition_probe" > "$SIXDB_RESULTS/composition-checks.jsonl"
taskset -c "$SIXDB_CPU" "$object_bin/orbital_objects_views" > "$SIXDB_RESULTS/linux-views.jsonl"
taskset -c "$SIXDB_CPU" "$object_bin/orbital_objects_capabilities" > "$SIXDB_RESULTS/linux-capabilities.jsonl"
taskset -c "$SIXDB_CPU" "$object_bin/orbital_objects_composition_bench" --ms 20 --repetitions 3 > "$SIXDB_RESULTS/composition-forward.csv"
taskset -c "$SIXDB_CPU" "$object_bin/orbital_objects_composition_bench" --ms 20 --repetitions 3 --reverse > "$SIXDB_RESULTS/composition-reverse.csv"
python3 -B workbench/spikes/orbital-objects/composition-summary.py "$SIXDB_RESULTS" > "$SIXDB_RESULTS/summary-checks.json"
lscpu > "$SIXDB_RESULTS/lscpu.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
