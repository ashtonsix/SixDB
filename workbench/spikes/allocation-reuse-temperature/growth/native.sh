#!/bin/bash
set -euo pipefail
python3 workbench/spikes/allocation-reuse-temperature/growth/run.py \
  --output "$SIXDB_RESULTS/comparison" --cpu "$SIXDB_CPU" \
  --tune "${SIXDB_GROWTH_TUNE:-generic}" \
  --trials "${SIXDB_GROWTH_TRIALS:-6}" \
  --operations "${SIXDB_GROWTH_OPERATIONS:-1048576}"
