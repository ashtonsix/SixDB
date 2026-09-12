#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Run through worker.py, or choose a results directory}"
: "${SIXDB_CPU:?Choose an allowed measurement CPU}"
exec python3 "$(dirname "$0")/run.py" \
  --output "$SIXDB_RESULTS/tuple-runtime" --cpu "$SIXDB_CPU" "$@"
