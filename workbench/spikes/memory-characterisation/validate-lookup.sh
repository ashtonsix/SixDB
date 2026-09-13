#!/bin/bash
set -euo pipefail
python3 workbench/spikes/memory-characterisation/initialise.py --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/lookup"
python3 - "$SIXDB_RESULTS/lookup/initialisation.json" <<'PY'
import json
from pathlib import Path
import sys
assert json.loads(Path(sys.argv[1]).read_text())['source'] == 'static-lookup'
print('Live CPU matched static lookup without running a probe')
PY
