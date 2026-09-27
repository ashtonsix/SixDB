#!/bin/bash
set -euo pipefail
python3 workbench/spikes/orbital-local-handoff/run.py --output "$SIXDB_RESULTS/study" "$@"
