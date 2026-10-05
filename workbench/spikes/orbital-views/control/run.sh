#!/bin/bash
set -euo pipefail
control_build=${SIXDB_CONTROL_BUILD:-build/spikes/orbital-views/control}
control_output=${SIXDB_RESULTS:-build/spikes/orbital-views/control-results}
mkdir -p "$control_output"
cmake -S workbench/spikes/orbital-views/control -B "$control_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH="${SIXDB_MARCH:-}" \
  -DSIXDB_TUNE="${SIXDB_TUNE:-generic}" > "$control_output/configure.log" 2>&1
cmake --build "$control_build" -j "${SIXDB_BUILD_JOBS:-2}" > "$control_output/build.log" 2>&1
"$control_build/control_probe" > "$control_output/probe.json"
python3 -c 'import json,os; print(json.dumps({"allowed_cpus":sorted(os.sched_getaffinity(0)),"logical_cpus":os.cpu_count()}))' \
  > "$control_output/affinity.json"
for trial in 1 2 3; do
  "$control_build/control_bench" > "$control_output/forward-$trial.csv"
  "$control_build/control_bench" --reverse > "$control_output/reverse-$trial.csv"
done
python3 -B workbench/spikes/orbital-views/control/summarize.py "$control_output"
python3 -B workbench/tools/compile_probe.py "$control_build" \
  --source mechanisms.cpp --source probe.cpp --source bench.cpp \
  --output "$control_output/compilation" > "$control_output/compile-probe.log" 2>&1
size "$control_build/libcontrol_mechanisms.a" "$control_build/control_bench" \
  > "$control_output/binary-size.txt"
