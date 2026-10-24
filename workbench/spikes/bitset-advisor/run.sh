#!/bin/bash
# Builds the trace, prices every plan on calibration classes and evaluation
# collections (checks each plan's answers first), then evaluates the advisor.
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose output}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
case "${1:-neon}" in
  avx512) march=znver5; tune=zen5;;
  neon) march=armv8.2-a+simd; tune=neoverse-v2;;
  *) exit 2;;
esac
build="build/bitset-advisor/${1:-neon}"
mkdir -p "$SIXDB_RESULTS"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=bitset-advisor \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target bitset_advisor_trace -j "${SIXDB_BUILD_JOBS:-1}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
windows=$(python3 workbench/tools/datasets.py fetch workbench/spikes/bec256-composition/windows.json)
cp workbench/spikes/bec256-composition/windows.json "$SIXDB_RESULTS/inputs.json"
taskset -c "$SIXDB_CPU" "$build/workbench/spikes/bitset-advisor/bitset_advisor_trace" "$windows" \
  > "$SIXDB_RESULTS/prices.json" 2> "$SIXDB_RESULTS/checks.log"
python3 workbench/spikes/bitset-advisor/advise.py "$SIXDB_RESULTS/prices.json" \
  > "$SIXDB_RESULTS/advice.txt"
