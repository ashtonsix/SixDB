#!/bin/bash
set -euo pipefail
residency_output=${SIXDB_RESULTS:-build/spikes/orbital-views/residency-results}
mkdir -p "$residency_output"
clang++-21 --version > "$residency_output/compiler.txt"
uname -a > "$residency_output/kernel.txt"
cmake -S workbench/spikes/orbital-views/residency -B build/spikes/orbital-views/residency \
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_MARCH="${SIXDB_MARCH:-x86-64-v4}" -DSIXDB_TUNE="${SIXDB_TUNE:-zen5}" \
  > "$residency_output/configure.log" 2>&1
cmake --build build/spikes/orbital-views/residency -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$residency_output/build.log" 2>&1
residency_bin=build/spikes/orbital-views/residency/residency_probe
"$residency_bin" --check > "$residency_output/checks.csv"
residency_cpu=${SIXDB_CPU:-$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')}
for trial in 1 2 3; do
  taskset -c "$residency_cpu" "$residency_bin" > "$residency_output/forward-$trial.csv"
  taskset -c "$residency_cpu" "$residency_bin" --reverse > "$residency_output/reverse-$trial.csv"
  taskset -c "$residency_cpu" "$residency_bin" --memory > "$residency_output/memory-$trial.csv"
done
python3 workbench/spikes/orbital-views/residency/summarize.py "$residency_output"
