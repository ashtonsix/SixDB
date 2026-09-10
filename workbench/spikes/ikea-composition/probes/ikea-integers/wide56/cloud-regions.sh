#!/bin/bash
set -euo pipefail
target="$1"
python3 workbench/spikes/ikea-composition/probes/ikea-integers/wide56/run.py --target "$target" -- --pmu
if [[ "$target" == "zen5" || "$target" == "granite-rapids" ]]; then
    python3 workbench/spikes/ikea-composition/probes/ikea-integers/wide56/run.py --target "$target" --encode-region32 -- --suite bulk --pmu
fi
