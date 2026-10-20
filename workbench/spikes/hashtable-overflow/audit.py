#!/usr/bin/env python3
"""Audit the density panel against its actual request/operation grid."""
import argparse
import csv
import json
import math
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('results', type=Path)
p.add_argument('--levels', default='890,925,980')
a = p.parse_args()
root = a.results
arms = ['baseline', 'tag8', 'tag8_c30', 'tag8_c31', 'filter15', 'filter15_c30', 'filter15_c31', 'boost', 'abseil']
expected = set()
answers = {}
populations = {}
for case in ['30_10_0', '30_10_8', '30_10_64', '30_20_0', '30_20_8']:
    for level in map(int, a.levels.split(',')):
        target = level * (1024 if '_20_' in case else 1)
        for seed in [713, 991, 1729]:
            for arm in arms:
                name = f'{case}-spill{target}-seed{seed}-{arm}.csv'
                expected.add(name)
                rows = list(csv.DictReader((root / 'samples' / name).open()))
                phases = {r['phase'] for r in rows}
                if phases != {'build', 'read', 'read_after_churn', 'replace_mixed', 'replace_existing', 'upsert50', 'refill', 'erase'}:
                    raise RuntimeError(f'incomplete phases {name}: {phases}')
                for phase, reps in [('build', {0}), ('replace_mixed', {0}), ('replace_existing', {1}),
                                    ('upsert50', {2}), ('refill', set(range(3, 12))), ('erase', set(range(2, 12)))]:
                    got = [int(r['rep']) for r in rows if r['phase'] == phase]
                    if len(got) != len(reps) or set(got) != reps:
                        raise RuntimeError(f'incomplete writes {name}: {phase} {got}')
                distances = ['0'] if arm == 'boost' else ['0', '32']
                wanted = set()
                for phase in ['read', 'read_after_churn']:
                    scopes = ['all', 'routed', 'primary'] if phase == 'read' else ['all']
                    for scope in scopes:
                        for miss in (['0'] if scope == 'primary' else ['0', '50', '100']):
                            for distance in distances:
                                for rep in ['0', '1', '2']:
                                    wanted.add((phase, scope, miss, distance, rep))
                got = [(r['phase'], r['scope'], r['miss'], r['prefetch'], r['rep'])
                       for r in rows if r['phase'] in ['read', 'read_after_churn']]
                if len(got) != len(wanted) or set(got) != wanted:
                    raise RuntimeError(f'incomplete read grid {name}')
                popkey = (case, target, seed)
                for r in rows:
                    if not math.isfinite(float(r['ns'])) or float(r['ns']) <= 0:
                        raise RuntimeError(f'invalid timing {name}')
                    if populations.setdefault(popkey, r['n']) != r['n']:
                        raise RuntimeError(f'different input cardinalities {name}')
                    if r['phase'] in ['read', 'read_after_churn']:
                        key = popkey + (r['phase'], r['scope'], r['miss'])
                        answer = tuple(r[k] for k in ['hits', 'keys', 'values'])
                        if answers.setdefault(key, answer) != answer:
                            raise RuntimeError(f'different complete results {name}: {key}')
                first = rows[0]
                if arm not in ['boost', 'abseil']:
                    if int(first['spill']) != target:
                        raise RuntimeError(f'wrong Swiss population {name}')
                    limit = 30 if arm.endswith('c30') else 31 if arm.endswith('c31') else 28
                    if int(first['ceiling32']) != limit or float(first['swiss_load']) > 100 * limit / 32 + 0.001:
                        raise RuntimeError(f'wrong Swiss ceiling {name}')
actual = {p.name for p in (root / 'samples').glob('*.csv')}
if actual != expected:
    raise RuntimeError(f'grid difference: missing={expected - actual}, extra={actual - expected}')
checks = json.loads((root / 'codegen/comparison.json').read_text())
if len(checks) != 36 or not all(c['identical'] for c in checks):
    raise RuntimeError('primary codegen changed')
for variant in ['baseline', 'tag8', 'filter15']:
    if 'directed full-cycle continuation: passed' not in (root / f'check-{variant}.txt').read_text():
        raise RuntimeError(f'missing directed continuation check {variant}')
text = f'{len(expected)} complete callers; matched cardinalities and full read results; all write rounds; 36 identical primary loops\n'
(root / 'audit.txt').write_text(text)
print(text, end='')
