#!/bin/bash
set -euo pipefail
python3 workbench/spikes/memory-characterisation/run.py --profile quick --mib 256 --seed 918273 --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/study" "$@"
