#!/bin/bash
# Minimal live group lifecycle check; no compiler or measurement workload.
set -euo pipefail
python3 - <<'PY'
import json, os
from pathlib import Path
data = {key: os.environ[key] for key in ('SIXDB_JOB', 'SIXDB_GROUP_MEMBER', 'SIXDB_GROUP_URI')}
assert all(data.values())
(Path(os.environ['SIXDB_RESULTS']) / 'group-smoke.json').write_text(json.dumps(data, indent=2) + '\n')
PY
