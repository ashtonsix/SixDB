#!/bin/bash
set -euo pipefail
target="$1"
python3 workbench/spikes/ikea-composition/probes/ikea-integers/run.py --target "$target" --kernels-only -- \
  --suite capacity --min-power 10 --max-power 30 --power-step 10 \
  --samples 1048576 --repetitions 3 --pmu
payload=536870912
if [[ "$target" == granite-rapids ]]; then payload=1073741824; fi
python3 workbench/spikes/ikea-composition/probes/ikea-integers/run.py --target "$target" --kernels-only -- \
  --suite capacity --payload-bytes "$payload" \
  --samples 1048576 --repetitions 3 --pmu
