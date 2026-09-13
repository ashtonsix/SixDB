#!/bin/bash
set -euo pipefail
python3 workbench/spikes/layout-analyser/run.py --cpu "$SIXDB_CPU" \
  --march x86-64-v3 --tune "${SIXDB_LAYOUT_TUNE:-generic}" \
  --output "$SIXDB_RESULTS/consumers" "$@"
