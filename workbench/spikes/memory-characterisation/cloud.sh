#!/bin/bash
set -euo pipefail
if [[ "${SIXDB_PREPARE_INSTANCE_STORE:-0}" == 1 ]]; then
    bash workbench/spikes/memory-characterisation/instance-store.sh
    export SIXDB_INSTANCE_STORE_DIR=/mnt/sixdb-characterisation-store
fi
python3 workbench/spikes/memory-characterisation/run.py --profile screen --smt --system --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/study" "$@"
python3 workbench/spikes/memory-characterisation/run.py --profile quick --mib 256 --seed 918273 --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/repeat"
