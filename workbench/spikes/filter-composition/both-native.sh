#!/bin/bash
set -euo pipefail
result_root="$SIXDB_RESULTS"
mkdir -p "$result_root/runtime" "$result_root/extra"
SIXDB_RESULTS="$result_root/runtime" bash workbench/spikes/filter-composition/native.sh
SIXDB_RESULTS="$result_root/extra" bash workbench/spikes/filter-composition/extra-native.sh
