#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) march=armv8-a+simd; tune=neoverse-v2 ;;
  *) tune="${DIRECTORY_TUNE:-zen5}"; if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
esac
python3 workbench/spikes/index-directories/run.py \
  --operations 65536 --repetitions 5 --writes 128 --cpu "$SIXDB_CPU" \
  --march "$march" --tune "$tune" --output "$SIXDB_RESULTS/measurements" \
  --workspace build/workspaces/index-directories-worker "$@"
