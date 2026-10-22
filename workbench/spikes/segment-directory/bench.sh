#!/bin/bash
# Read timings on a worker, e.g. from the SixDB root:
#   python3 workbench/tools/worker.py run workbench/spikes/segment-directory/bench.sh \
#     --machine neoverse-v2 --instance-type c8g.2xlarge
set -euo pipefail
cmake --preset release -DSIXDB_SPIKES=segment-directory >/dev/null
cmake --build --preset release --target segment_directory segment_directory_bench -j "${SIXDB_BUILD_JOBS:-1}" >/dev/null
d=build/clang/release/workbench/spikes/segment-directory
out=${SIXDB_RESULTS:-build/segment-directory}
mkdir -p "$out"
$d/segment_directory --check > "$out/check.txt"
run() { taskset -c "${SIXDB_CPU:-0}" $d/segment_directory_bench "$@" | tail -n +2; }
{
  echo variant,layer,j,count,placement,metric,median_ns,min_ns,reps
  for n in 65536 1048576 4194304; do
    run 4 0 $n spread 1048576 7 flat tree tree-nosum
    for p in spread dense; do
      run 3 4 $n $p 1048576 7 tree tree-hash tree-inline24 natural16 natural24
    done
    run 4 4 $n spread 1048576 7 tree tree-hash tree-inline24 natural24
  done
} > "$out/reads.csv"
