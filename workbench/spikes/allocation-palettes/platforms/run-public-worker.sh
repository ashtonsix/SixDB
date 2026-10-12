#!/usr/bin/env bash
# Recover the fixed baseline; the worker's ordinary source capture is candidate.
set -euo pipefail
candidate="${SIXDB_SOURCE:-$PWD}"
recovered="build/allocation-palettes/public-baseline/${SIXDB_JOB:-local-$$}"
results="${SIXDB_RESULTS:-build/allocation-palettes}"
baseline_ref="${PALETTE_PUBLIC_BASELINE_REF:-$candidate/workbench/spikes/allocation-palettes/platforms/public-original.json}"
python3 - <<'PY'
import os
page = os.sysconf('SC_PAGESIZE')
assert page == int(os.environ.get('PALETTE_PUBLIC_EXPECTED_PAGE', page)), f'unexpected native page: {page}'
PY
if [[ "${PALETTE_VALIDATE:-0}" == 1 ]]; then
  SIXDB_RESULTS="$results/validation" PALETTE_EXPECTED_PAGE="${PALETTE_PUBLIC_EXPECTED_PAGE:-$(getconf PAGESIZE)}" \
    bash "$candidate/workbench/spikes/allocation-palettes/platforms/validate.sh"
fi
if [[ "${PALETTE_PUBLIC_ONLY:-0}" != 1 ]]; then
  SIXDB_RESULTS="$results/mechanism" PALETTE_REPETITIONS=3 PALETTE_POPULATION=1024 PALETTE_ROUNDS=4 \
    PALETTE_CASES=current,selected,partition-only,full-geometric-16,full-geometric-32,full-even-16,full-even-32,band-geometric-16,band-geometric-32,band-even-16,band-even-32,band-geometric-15,band-geometric-31,only-b \
    bash "$candidate/workbench/spikes/allocation-palettes/platforms/run-performance.sh" \
    --class-lookup 1 --class-once 1
fi
python3 "$candidate/workbench/tools/artifacts.py" fetch \
  "$baseline_ref" "$recovered"
cp "$baseline_ref" "$results/public-baseline-reference.json"
python3 - "$recovered" <<'PY'
import hashlib, json, sys
from pathlib import Path
root = Path(sys.argv[1])
receipt = json.loads((root / 'source.capture.json').read_text())
hashes = receipt['source_files_sha256']
digest = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
assert digest == receipt['source_digest'], 'baseline capture digest differs'
for name, expected in hashes.items():
    path = root / 'source' / name
    assert path.resolve().is_relative_to((root / 'source').resolve())
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, f'baseline changed: {name}'
print(f'Verified baseline: {len(hashes)} files, {digest}')
PY
bash "$candidate/workbench/spikes/allocation-palettes/platforms/run-public.sh" \
  "$recovered/source" "$candidate" "$results/public"
if [[ "${PALETTE_READY_CLASS_PROBE:-0}" == 1 ]]; then
  bash "$candidate/workbench/spikes/allocation-palettes/platforms/probe-ready-class.sh"
fi
