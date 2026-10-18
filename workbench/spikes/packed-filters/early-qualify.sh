#!/bin/bash
set -euo pipefail
progress() {
  PYTHONPATH="$SIXDB_SOURCE/workbench/tools" python3 - "$1" <<'PY'
import sys
from worker_context import GroupContext
GroupContext.from_env().progress(sys.argv[1])
PY
}
progress full-matrix
bash workbench/spikes/packed-filters/early-native.sh
progress focused-rechecks
bash workbench/spikes/packed-filters/early-focus.sh
progress finished
