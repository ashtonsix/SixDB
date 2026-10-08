#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose results directory or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
if ! pkg-config --exists lmdb; then
  sudo -n apt-get update
  sudo -n apt-get install -y liblmdb-dev
fi
out="$SIXDB_RESULTS/orbital-allocation"
mkdir -p "$out"
build=build/benchmarks/orbital-tooling
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=orbital -DSIXDB_MARCH="${ORBITAL_MARCH:-znver5}" \
  -DSIXDB_TUNE="${ORBITAL_TUNE:-zen5}"
/usr/bin/time -v -o "$out/build.resources" cmake --build "$build" \
  --target orbital_lifecycle_bench -j "${SIXDB_BUILD_JOBS:-1}"
python3 -B workbench/benchmarks/orbital/pressure.py \
  "$build/workbench/benchmarks/orbital/orbital_lifecycle_bench" \
  --output "$out/pressure" --cpu "$SIXDB_CPU" \
  --rounds "${ORBITAL_PRESSURE_ROUNDS:-64}" \
  --repetitions "${ORBITAL_REPETITIONS:-3}" "$@"
