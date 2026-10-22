#!/usr/bin/env bash
# Directory-only counter matrix. Run on Linux from the SixDB root:
#   workbench/spikes/segment-directory/run.sh > build/segment-directory/counters.txt
set -euo pipefail
cmake --preset dev -B build/segment-directory -DSIXDB_SPIKES=segment-directory >/dev/null
cmake --build build/segment-directory --target segment_directory >/dev/null
b=build/segment-directory/workbench/spikes/segment-directory/segment_directory
$b --check >/dev/null
for n in 4096 65536 1048576; do
  for p in spread dense; do
    $b run 4 0 $n $p flat tree tree-untiled tree-leaf64 tree-leaf256
    $b run 3 4 $n $p tree tree-hash tree-inline24 tree-inline64
    $b run 4 4 $n $p tree tree-hash tree-inline24 tree-inline64
  done
done
# Old inline-dirent controls copy whole natural partitions per edit.
for n in 4096 65536 1048576; do
  ops=$(( n > 65536 ? 20 : 400 ))
  for p in spread dense; do
    SEGDIR_OPS=$ops $b run 3 4 $n $p natural24 natural16
    SEGDIR_OPS=$ops $b run 4 4 $n $p natural24
  done
done
for p in spread dense; do
  $b run 2 4 16384 $p tree tree-hash tree-inline24 tree-inline64
  SEGDIR_OPS=400 $b run 2 4 16384 $p natural24 natural8
done
# Edit grain beyond single uniform edits: batches, churn and appends.
for p in spread dense; do
  $b run 4 0 65536 $p scenario-tree scenario-halves scenario-flat
  $b run 4 0 1048576 $p scenario-tree scenario-halves
  $b run 3 4 1048576 $p scenario-tree
done
