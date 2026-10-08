#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose results directory or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
if ! pkg-config --exists lmdb; then
  sudo -n apt-get update
  sudo -n apt-get install -y liblmdb-dev
fi
build=build/benchmarks/orbital-pmu
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=orbital -DSIXDB_MARCH="${ORBITAL_MARCH:-znver5}" \
  -DSIXDB_TUNE="${ORBITAL_TUNE:-zen5}"
cmake --build "$build" --target orbital_pmu_probe -j "${SIXDB_BUILD_JOBS:-1}"
python3 -B workbench/benchmarks/orbital/pmu_probe.py \
  "$build/workbench/benchmarks/orbital/orbital_pmu_probe" \
  --output "$SIXDB_RESULTS/pmu-availability" --cpu "$SIXDB_CPU" --compare-root "$@"
