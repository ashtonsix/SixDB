#!/bin/bash
set -euo pipefail
python3 workbench/spikes/allocation-reuse-temperature/slack/run.py \
  --output "$SIXDB_RESULTS/comparison" --cpu "$SIXDB_CPU" \
  --tune "${SIXDB_REUSE_TUNE:-generic}" \
  --trials "${SIXDB_REUSE_TRIALS:-6}" --operations "${SIXDB_REUSE_OPERATIONS:-32768}" \
  --rounds "${SIXDB_REUSE_ROUNDS:-4}" --slots "${SIXDB_REUSE_SLOTS:-16}"
python3 workbench/spikes/allocation-reuse-temperature/slack/report.py "$SIXDB_RESULTS/comparison"
for trial in {0..11}; do
  case $((trial % 3)) in
    0) modes=(control dontneed free);;
    1) modes=(dontneed free control);;
    2) modes=(free control dontneed);;
  esac
  for mode in "${modes[@]}"; do
    taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/comparison/build/tail_probe" "$mode" \
      > "$SIXDB_RESULTS/tail-$trial-$mode.csv"
  done
done
