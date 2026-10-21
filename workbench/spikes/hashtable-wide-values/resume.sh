#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-wide-values
root="$SIXDB_SOURCE/build/wide-resume"
mkdir -p "$root" "$SIXDB_RESULTS"
sudo -n apt-get update > "$SIXDB_RESULTS/packages.log" 2>&1
sudo -n apt-get install -y libfmt-dev >> "$SIXDB_RESULTS/packages.log" 2>&1
aws s3 cp --only-show-errors \
  s3://calico-fleet-artifacts/sixdb/artifacts/sha256/dd117b74e334c5a2f5baa07f23e90051b86a3801580bfc3f1b5ff099ea0e6d7e.tar.gz \
  "$root/previous.tar.gz"
python3 "$study/resume.py" "$root/previous.tar.gz" "$SIXDB_RESULTS/resumed" "$root/frozen"
python3 "$root/frozen/$study/run.py" "$SIXDB_RESULTS/resumed/binaries" "$SIXDB_RESULTS/resumed" \
  --mode full --cpu "$SIXDB_CPU"
python3 - "$SIXDB_RESULTS/resumed" <<'PY'
import json,sys
from pathlib import Path
root=Path(sys.argv[1]); plan=json.loads((root/'plan.json').read_text())
assert all(json.loads((root/'samples'/(p['case']+'.json')).read_text())['returncode']==0 for p in plan)
(root/'completion.json').write_text(json.dumps({'planned':len(plan),'completed':len(plan),'resumed_from':'20261021T130039Z-b8b3dd4e','old_worker_state':'interrupted'},indent=2)+'\n')
PY
bash "$study/native-loom.sh"
