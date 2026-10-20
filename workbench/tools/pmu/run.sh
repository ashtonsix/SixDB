#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose results directory or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
build=build/tools/pmu
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS= -DSIXDB_SPIKES= \
  -DSIXDB_MARCH="${SIXDB_MARCH:-}" -DSIXDB_TUNE="${SIXDB_TUNE:-generic}"
cmake --build "$build" --target sixdb_pmu_probe -j "${SIXDB_BUILD_JOBS:-1}"
python3 -B workbench/tools/pmu/probe.py \
  "$build/workbench/tools/pmu/sixdb_pmu_probe" \
  --output "$SIXDB_RESULTS/pmu-availability" --cpu "$SIXDB_CPU" --compare-root "$@"
