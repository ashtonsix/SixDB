#!/bin/bash
# engine::directory read timings on a worker, e.g. from the SixDB root:
#   python3 workbench/tools/worker.py run workbench/spikes/segment-directory/engine-reads.sh \
#     --machine neoverse-v2 --instance-type c8g.4xlarge --env SIXDB_BUILD_JOBS=8
set -euo pipefail
cmake --preset release -DSIXDB_SPIKES=segment-directory >/dev/null
cmake --build --preset release --target segment_directory_engine_reads engine_directory_check -j "${SIXDB_BUILD_JOBS:-1}" >/dev/null
out=${SIXDB_RESULTS:-build/segment-directory}
mkdir -p "$out"
build/clang/release/engine/engine_directory_check > "$out/check.txt"
export SEGDIR_ARENA_MB=${SEGDIR_ARENA_MB:-16384}
b=build/clang/release/workbench/spikes/segment-directory/segment_directory_engine_reads
{
  echo variant,layer,j,count,placement,metric,median_ns,min_ns,reps
  for n in 1048576 4194304; do
    taskset -c "${SIXDB_CPU:-0}" $b 4 0 $n spread 1048576 7
    taskset -c "${SIXDB_CPU:-0}" $b 3 4 $n spread 1048576 7
  done
} > "$out/engine-reads.csv"
