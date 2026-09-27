#!/bin/bash
set -euo pipefail
root_results="$SIXDB_RESULTS"
progress() {
  python3 - "$1" <<'PY'
import sys
sys.path.insert(0, 'workbench/tools')
from worker_context import GroupContext
GroupContext.from_env().progress(sys.argv[1])
PY
}
progress native-load
SIXDB_RESULTS="$root_results/native-load" bash workbench/spikes/ikea-neoverse-v3/native-load.sh
progress tuple-dispatch
SIXDB_RESULTS="$root_results/tuple-dispatch" bash workbench/spikes/ikea-neoverse-v3/tuple-dispatch.sh
progress complete
