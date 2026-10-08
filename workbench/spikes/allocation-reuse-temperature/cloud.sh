#!/bin/bash
set -euo pipefail
python3 workbench/spikes/allocation-reuse-temperature/run.py \
  --output "$SIXDB_RESULTS/comparison" --cpu "$SIXDB_CPU" --tune zen5 "$@"
if [[ "${SIXDB_REUSE_DIAGNOSTICS:-0}" == 1 ]]; then
  python3 workbench/spikes/allocation-reuse-temperature/diagnostics.py \
    --comparison "$SIXDB_RESULTS/comparison"
fi
if [[ "${SIXDB_REUSE_POLICY_PROBE:-0}" == 1 ]]; then
  cmake --build "$SIXDB_RESULTS/comparison/build-baseline" \
    --target allocation_reuse_temperature_probe -j "$SIXDB_BUILD_JOBS"
  "$SIXDB_RESULTS/comparison/build-baseline/workbench/spikes/allocation-reuse-temperature/allocation_reuse_temperature_probe" \
    --cpu "$SIXDB_CPU" --repetitions 7 > "$SIXDB_RESULTS/policies.csv"
fi
