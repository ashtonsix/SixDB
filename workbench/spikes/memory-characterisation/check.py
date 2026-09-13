#!/usr/bin/env python3
"""Focused checks for inference refusal, repeated-run qualification and lookup invalidation."""
from copy import deepcopy
import json
import hashlib
from pathlib import Path
from lookup import consistent, match, qualify


def curve(values):
    points = [{'k': k, 'ns_per_load': value} for k, value in values]
    return {'curve': points, 'serial_ns_per_load': points[0]['ns_per_load'],
            'smallest_k_within_10pct_of_best': min(points, key=lambda p: p['ns_per_load'])['k'],
            'right_censored': points[-1]['ns_per_load'] == min(p['ns_per_load'] for p in points)}

# A flat optimum does not justify treating the smallest argmin as a hardware count.
a = curve([(1, 100), (8, 14), (16, 10), (24, 10.5), (32, 12)])
b = curve([(1, 101), (8, 15), (16, 10.5), (24, 10), (32, 13)])
assert qualify([a, b])['candidate_k'] == [16, 24]
assert qualify([a])['status'] == 'insufficient-independent-runs'
c = curve([(1, 200), (8, 30), (16, 21), (24, 20), (32, 26)])
assert qualify([a, c])['status'] == 'unresolved-repeatability'
points = [{'controls_separated': True, 'normalised_saving': .8} for _ in range(5)]
assert consistent(points) == 'observed-benefit'
points[-1]['controls_separated'] = False
assert consistent(points) == 'unresolved'
points[-1]['controls_separated'] = True
points[-1]['normalised_saving'] = -.1
assert consistent(points) == 'unresolved'
root = Path(__file__).resolve().parent
database = json.loads((root / 'lookup.json').read_text())
for key, entry in database['entries'].items():
    hardware = {'lookup_key': key, 'lookup_context': entry['lookup_context']}
    assert match(hardware, database) is entry
    changed = deepcopy(hardware)
    changed['lookup_context']['kernel'] += '-changed'
    assert match(changed, database) is None
    changed = deepcopy(hardware)
    changed['lookup_context']['smt_width'] += 1
    assert match(changed, database) is None
    changed = deepcopy(hardware)
    changed['lookup_context']['base_page'] *= 4
    assert match(changed, database) is None
assert match({'lookup_key': 'unknown', 'lookup_context': {}}, database) is None
assert match(hardware, {'format': 999, 'entries': database['entries']}) is None
for manifest in (root / 'evidence').rglob('provenance.json'):
    for name, digest in json.loads(manifest.read_text()).get('selected_files_sha256', {}).items():
        assert hashlib.sha256((manifest.parent / name).read_bytes()).hexdigest() == digest
print(f'PASS: inference refusal, repeatability and {len(database["entries"])} lookup invalidation cases')
