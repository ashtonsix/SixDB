#!/bin/bash
# Interior fanout under Orbital visits; same setup as bench.sh.
set -euo pipefail
cmake --preset release -DSIXDB_SPIKES=segment-directory >/dev/null
cmake --build --preset release --target segment_directory_bench -j "${SIXDB_BUILD_JOBS:-1}" >/dev/null
d=build/clang/release/workbench/spikes/segment-directory
out=${SIXDB_RESULTS:-build/segment-directory}
mkdir -p "$out"
export SEGDIR_ARENA_MB=${SEGDIR_ARENA_MB:-16384}
run() { taskset -c "${SIXDB_CPU:-0}" $d/segment_directory_bench "$@" | tail -n +2; }
{
  echo variant,layer,j,count,placement,metric,median_ns,min_ns,reps
  for n in 1048576 4194304; do
    run 4 0 $n spread 1048576 7 flat@orbital tree@orbital tree-inner512@orbital flat tree tree-inner512
    run 3 4 $n spread 1048576 7 tree@orbital tree-inner512@orbital natural16@orbital
  done
} > "$out/inner.csv"
