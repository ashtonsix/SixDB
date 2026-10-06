#!/bin/bash
set -euo pipefail
python3 workbench/spikes/loom-execution/run.py --output "$SIXDB_RESULTS/study" "$@"
