#!/usr/bin/env python3
"""Verify the deliberately frozen independent wire fixture."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
for entry in json.loads((root / 'provenance.json').read_text())['files']:
    path = root / entry['retained']
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != entry['retained_sha256']:
        raise SystemExit(f'Frozen reference changed: {path}; inspect its independent provenance')
print('Ikea2: independent reference hashes verified')
