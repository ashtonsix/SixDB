#!/bin/bash
set -euo pipefail
view_output=${SIXDB_RESULTS:-build/spikes/orbital-views/results}
mkdir -p "$view_output"
view_isa=${SIXDB_MARCH:-x86-64-v4}
view_tune=${SIXDB_TUNE:-zen5}
cmake -S . -B build/spikes/orbital-views -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=orbital-views -DSIXDB_MARCH="$view_isa" -DSIXDB_TUNE="$view_tune" \
  > "$view_output/configure.log" 2>&1
cmake --build build/spikes/orbital-views --target orbital_views_probe orbital_views_bench \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$view_output/build.log" 2>&1
view_bin=build/spikes/orbital-views/workbench/spikes/orbital-views
"$view_bin/orbital_views_probe" > "$view_output/checks.jsonl"
view_cpu=${SIXDB_CPU:-$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')}
for trial in 1 2 3; do
  taskset -c "$view_cpu" "$view_bin/orbital_views_bench" > "$view_output/forward-$trial.csv"
  taskset -c "$view_cpu" "$view_bin/orbital_views_bench" --reverse > "$view_output/reverse-$trial.csv"
done
python3 -B workbench/spikes/orbital-views/summarize.py "$view_output" > "$view_output/summary-checks.json"
lscpu > "$view_output/lscpu.txt"
clang++-21 --version > "$view_output/compiler.txt"
uname -a > "$view_output/kernel.txt"
