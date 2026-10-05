#!/bin/bash
set -euo pipefail
bash workbench/spikes/orbital-views/check.sh
view_output=${SIXDB_RESULTS:-build/spikes/orbital-views/composition-results}
cmake --build build/spikes/orbital-views --target orbital_views_pattern_probe \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$view_output/pattern-build.log" 2>&1
view_cpu=${SIXDB_CPU:-$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')}
for trial in 1 2 3; do
  taskset -c "$view_cpu" build/spikes/orbital-views/workbench/spikes/orbital-views/orbital_views_pattern_probe \
    > "$view_output/forward-$trial.csv"
  taskset -c "$view_cpu" build/spikes/orbital-views/workbench/spikes/orbital-views/orbital_views_pattern_probe --reverse \
    > "$view_output/reverse-$trial.csv"
done
python3 -B workbench/spikes/orbital-views/pattern-summary.py "$view_output"
