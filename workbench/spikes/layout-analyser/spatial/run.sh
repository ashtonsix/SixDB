#!/bin/bash
set -euo pipefail
python3 workbench/spikes/layout-analyser/spatial/run.py \
  --profile screen --cpu "${SIXDB_CPU:?worker CPU required}" \
  --output "${SIXDB_RESULTS:?worker results directory required}/spatial" "$@"
