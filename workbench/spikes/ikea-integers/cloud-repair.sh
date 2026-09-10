#!/bin/bash
set -euo pipefail
target="$1"
reader="${2:-fragment-classes}"
extra=()
if [[ "${3:-}" == register-masks ]]; then extra+=(--scan-register-masks); fi
for width in 5 7; do
  python3 workbench/spikes/ikea-integers/run.py --target "$target" --scan-reader "$reader" "${extra[@]}" --kernels-only -- \
    --suite all --layout scan --width "$width" \
    --min-power 10 --max-power 30 --power-step 20 \
    --samples 1048576 --repetitions 5 --bulk-bytes 524288 --pmu
done
