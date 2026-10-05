#!/bin/bash
set -euo pipefail
recycle_output=${SIXDB_RESULTS:-build/spikes/orbital-views/recycling-results}
mkdir -p "$recycle_output"
cmake -S . -B build/spikes/orbital-views -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=orbital-views -DSIXDB_MARCH="${SIXDB_MARCH:-x86-64-v4}" -DSIXDB_TUNE="${SIXDB_TUNE:-zen5}" \
  > "$recycle_output/configure.log" 2>&1
cmake --build build/spikes/orbital-views --target orbital_views_recycling_probe \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$recycle_output/build.log" 2>&1
recycle_bin=build/spikes/orbital-views/workbench/spikes/orbital-views/orbital_views_recycling_probe
"$recycle_bin" > "$recycle_output/checks.jsonl"
recycle_cpu=${SIXDB_CPU:-$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')}
for trial in 1 2 3; do
  taskset -c "$recycle_cpu" "$recycle_bin" --bench > "$recycle_output/forward-$trial.csv"
  taskset -c "$recycle_cpu" "$recycle_bin" --reverse > "$recycle_output/reverse-$trial.csv"
done
python3 -B workbench/spikes/orbital-views/recycle-summary.py "$recycle_output" > "$recycle_output/summary-checks.json"
