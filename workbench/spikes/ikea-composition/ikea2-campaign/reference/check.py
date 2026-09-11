#!/usr/bin/env python3
"""Verify the explicitly isolated frozen predecessor's retained source hashes."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
entries = json.loads((root / 'provenance.json').read_text())['files']
for entry in entries:
    path = root / entry['retained']
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != entry['retained_sha256']:
        raise SystemExit(f'Frozen predecessor changed: {path}; inspect provenance')
print(f'Ikea predecessor: {len(entries)} isolated source hashes verified')
