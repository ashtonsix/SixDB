#!/bin/bash
# Usage: worker.py run workbench/spikes/frame-phase-2/seriespack/maskload.sh --machine zen5
set -euo pipefail
mkdir -p build && clang++-21 -std=c++23 -O2 -march=znver5 workbench/spikes/frame-phase-2/seriespack/maskload.cpp -o build/maskload
taskset -c "$SIXDB_CPU" build/maskload > "$SIXDB_RESULTS/maskload.jsonl"
