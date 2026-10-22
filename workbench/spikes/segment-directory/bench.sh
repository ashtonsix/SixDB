#!/bin/bash
# Read timings on a worker, e.g. from the SixDB root:
#   python3 workbench/tools/worker.py run workbench/spikes/segment-directory/bench.sh \
#     --machine neoverse-v2 --instance-type c8g.4xlarge --env SIXDB_BUILD_JOBS=8
# The retained @orbital rows came from an engine::material reader removed when engine::directory graduated.
set -euo pipefail
cmake --preset release -DSIXDB_SPIKES=segment-directory >/dev/null
cmake --build --preset release --target segment_directory segment_directory_bench -j "${SIXDB_BUILD_JOBS:-1}" >/dev/null
d=build/clang/release/workbench/spikes/segment-directory
out=${SIXDB_RESULTS:-build/segment-directory}
mkdir -p "$out"
$d/segment_directory --check > "$out/check.txt"
export SEGDIR_ARENA_MB=${SEGDIR_ARENA_MB:-16384}
run() { taskset -c "${SIXDB_CPU:-0}" $d/segment_directory_bench "$@" | tail -n +2; }
{
  echo variant,layer,j,count,placement,metric,median_ns,min_ns,reps
  for n in 65536 1048576 4194304; do
    run 4 0 $n spread 1048576 7 flat tree
    for p in spread dense; do
      run 3 4 $n $p 1048576 7 tree tree-inline24 natural16
    done
  done
} > "$out/reads.csv"
