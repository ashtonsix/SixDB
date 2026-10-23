#!/bin/bash
# Relocatable-handle timing on a worker with at least four allowed CPUs.
set -euo pipefail
: "${SIXDB_RESULTS:?}"
config=(-DSIXDB_SPIKES=orbital-hot-paths -DSIXDB_TUNE="${SIXDB_TUNE:-generic}")
cmake --preset release -B build/hot "${config[@]}" > "$SIXDB_RESULTS/configure.log"
cmake --build build/hot --target orbital_hot_relocatable -j "${SIXDB_BUILD_JOBS:-4}" > "$SIXDB_RESULTS/build.log"
allowed=($(python3 -c 'import os; print(*sorted(os.sched_getaffinity(0)))'))
cpus=$(IFS=,; echo "${allowed[*]:0:4}")
for trial in $(seq 1 "${HOT_TRIALS:-3}"); do
  build/hot/workbench/spikes/orbital-hot-paths/orbital_hot_relocatable --cpus "$cpus" --seconds 2 \
    > "$SIXDB_RESULTS/relocatable-$trial.csv"
done
