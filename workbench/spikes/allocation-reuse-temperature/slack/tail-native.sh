#!/bin/bash
set -euo pipefail
study=workbench/spikes/allocation-reuse-temperature/slack
variants="$SIXDB_RESULTS/variants"
python3 - "$variants" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, 'workbench/spikes/allocation-reuse-temperature/slack')
from variants import write_variants
write_variants(Path('orbital/src/allocation/window.cpp'), Path(sys.argv[1]))
PY
cmake -S "$study" -B build/window-tail-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWINDOW_VARIANTS="$variants" -DSIXDB_MARCH=native -DSIXDB_TUNE="${SIXDB_REUSE_TUNE:-generic}" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/window-tail-native --target tail_probe -j "$SIXDB_BUILD_JOBS" \
  > "$SIXDB_RESULTS/build.log" 2>&1
cp build/window-tail-native/tail_probe "$SIXDB_RESULTS/tail_probe"
for trial in {0..11}; do
  case $((trial % 3)) in
    0) modes=(control dontneed free);;
    1) modes=(dontneed free control);;
    2) modes=(free control dontneed);;
  esac
  for mode in "${modes[@]}"; do
    taskset -c "$SIXDB_CPU" build/window-tail-native/tail_probe "$mode" \
      > "$SIXDB_RESULTS/tail-$trial-$mode.csv"
  done
done
