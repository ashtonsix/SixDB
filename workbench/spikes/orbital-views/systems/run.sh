#!/bin/bash
set -euo pipefail
systems_build=${SIXDB_SYSTEMS_BUILD:-build/spikes/orbital-views/systems}
systems_output=${SIXDB_RESULTS:-build/spikes/orbital-views/systems-results}
mkdir -p "$systems_output"
cmake -S workbench/spikes/orbital-views/systems -B "$systems_build" -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH="${SIXDB_MARCH:-}" -DSIXDB_TUNE="${SIXDB_TUNE:-generic}" \
  > "$systems_output/configure.log" 2>&1
cmake --build "$systems_build" --target resource_probe journal_probe composition_probe unit_boundary intent_probe binding_probe \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$systems_output/build.log" 2>&1
"$systems_build/composition_probe" > "$systems_output/composition.txt"
"$systems_build/unit_boundary" > "$systems_output/unit-boundary.json"
"$systems_build/intent_probe" > "$systems_output/intent.json"
"$systems_build/binding_probe" > "$systems_output/binding.txt"
systems_cpu=$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')
for probe in resource journal; do
  taskset -c "$systems_cpu" "$systems_build/${probe}_probe" --smoke > "$systems_output/$probe-smoke.csv"
  for trial in 1 2 3; do
    taskset -c "$systems_cpu" "$systems_build/${probe}_probe" > "$systems_output/$probe-forward-$trial.csv"
    taskset -c "$systems_cpu" "$systems_build/${probe}_probe" --reverse > "$systems_output/$probe-reverse-$trial.csv"
  done
done
findmnt -T /tmp -o TARGET,SOURCE,FSTYPE,OPTIONS > "$systems_output/filesystem.txt"
cat /proc/self/limits > "$systems_output/limits.txt"
python3 -B workbench/spikes/orbital-views/systems/summarize.py "$systems_output"
