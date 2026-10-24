#!/bin/bash
# Builds and runs the spikes that consume ikea::bec, checking the module rename.
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose output}"
build=build/bec/consumers
mkdir -p "$SIXDB_RESULTS"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH="${MARCH:-armv8.2-a+simd}" \
  -DSIXDB_TUNE="${TUNE:-neoverse-v2}" "-DSIXDB_SPIKES=bec256-composition;loom-execution;orbital-views;ikea-composition" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
targets=(bec_composition_check bec_casing_check bec_algebra_check bec_composition_bench
  loom_spike_bec loom_microtask ikea_heterogeneous_check ikea_contracts_check)
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
for t in bec_composition_check bec_casing_check bec_algebra_check; do
  "$build/workbench/spikes/bec256-composition/$t" >> "$SIXDB_RESULTS/checks.log"
done
"$build/ikea/ikea_contracts_check" >> "$SIXDB_RESULTS/checks.log"
find "$build/workbench/spikes" -name ikea_heterogeneous_check -type f -exec {} \; >> "$SIXDB_RESULTS/checks.log"
