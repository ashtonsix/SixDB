#!/bin/bash
set -euo pipefail
root_results="$SIXDB_RESULTS"
SIXDB_RESULTS="$root_results/tune" bash workbench/spikes/ikea-neoverse-v3/tune.sh
SIXDB_RESULTS="$root_results/series" bash workbench/spikes/ikea-neoverse-v3/series.sh
