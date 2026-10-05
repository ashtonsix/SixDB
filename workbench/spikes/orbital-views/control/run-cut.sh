#!/bin/bash
set -euo pipefail
cut_build=${SIXDB_CONTROL_BUILD:-build/spikes/orbital-views/control}
cut_output=${SIXDB_RESULTS:-build/spikes/orbital-views/cut-results}
mkdir -p "$cut_output"
cmake -S workbench/spikes/orbital-views/control -B "$cut_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH="${SIXDB_MARCH:-}" \
  -DSIXDB_TUNE="${SIXDB_TUNE:-generic}" > "$cut_output/configure.log" 2>&1
cmake --build "$cut_build" --target cut_probe cut_bench -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$cut_output/build.log" 2>&1
"$cut_build/cut_probe" > "$cut_output/probe.json"
python3 -c 'import json,os; print(json.dumps({"allowed_cpus":sorted(os.sched_getaffinity(0))}))' \
  > "$cut_output/affinity.json"
for trial in 1 2 3; do
  "$cut_build/cut_bench" > "$cut_output/cut-forward-$trial.csv"
  "$cut_build/cut_bench" --reverse > "$cut_output/cut-reverse-$trial.csv"
done
python3 -B workbench/spikes/orbital-views/control/summarize-cut.py "$cut_output"
python3 -B workbench/tools/compile_probe.py "$cut_build" \
  --source cut.cpp --source cut_probe.cpp --source cut_bench.cpp \
  --output "$cut_output/compilation" > "$cut_output/compile-probe.log" 2>&1
size "$cut_build/libcut_mechanisms.a" "$cut_build/cut_bench" > "$cut_output/binary-size.txt"
