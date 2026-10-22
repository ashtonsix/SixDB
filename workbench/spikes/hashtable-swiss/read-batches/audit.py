#!/usr/bin/env python3
"""Audit the complete width/control panel, not only successful sample files."""
import argparse
import csv
import json
import math
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('results', type=Path)
a = p.parse_args()
root = a.results
arms = json.loads((root / 'treatments.json').read_text()) if (root / 'treatments.json').exists() else ['group16', 'batch32', 'batch64', 'tail64', 'boost', 'abseil']
owners = [v for v in arms if v not in ['boost', 'abseil']]
if 'native' in owners:
    owner_checks = (root / 'owner-checks.txt').read_text()
    if '100% tests passed' not in owner_checks or '0 tests failed' not in owner_checks:
        raise RuntimeError('maintained owner checks failed')
if arms[0] != 'group16' or set(arms[-2:]) != {'boost','abseil'}:
    raise RuntimeError('require paired maintained and actual peer controls')
expected, answers, populations, capacities = set(), {}, {}, {}
cases = json.loads((root / 'cases.json').read_text()) if (root / 'cases.json').exists() else ['30_10_0', '30_10_8', '30_10_64', '30_20_0', '30_20_8']
for case in cases:
    cold = '_20_' in case
    settings = [('load', 85)]
    settings += [('spill', 890 * (1024 if cold else 1))]
    for setting, level in settings:
        for seed in [713, 991, 1729]:
            for arm in arms:
                name = f'{case}-{setting}{level}-seed{seed}-{arm}.csv'
                expected.add(name)
                rows = list(csv.DictReader((root / 'samples' / name).open()))
                if {r['phase'] for r in rows} != {'build', 'read', 'read_after_churn', 'replace_mixed', 'replace_existing', 'upsert50', 'refill', 'erase'}:
                    raise RuntimeError(f'incomplete phases {name}')
                for phase, reps in [('build', {0}), ('replace_mixed', {0}), ('replace_existing', {1}),
                                    ('upsert50', {2}), ('refill', set(range(3, 12))), ('erase', set(range(2, 12)))]:
                    got = [int(r['rep']) for r in rows if r['phase'] == phase]
                    if len(got) != len(reps) or set(got) != reps:
                        raise RuntimeError(f'incomplete writes {name}: {phase}')
                wanted = set()
                for phase in ['read', 'read_after_churn']:
                    for scope in (['all', 'routed', 'primary'] if phase == 'read' else ['all']):
                        for miss in (['0'] if scope == 'primary' else ['0', '50', '100']):
                            for distance in (['0'] if arm == 'boost' else ['0', '32']):
                                for rep in ['0', '1', '2']:
                                    wanted.add((phase, scope, miss, distance, rep))
                got = [(r['phase'], r['scope'], r['miss'], r['prefetch'], r['rep'])
                       for r in rows if r['phase'] in ['read', 'read_after_churn']]
                if len(got) != len(wanted) or set(got) != wanted:
                    raise RuntimeError(f'incomplete read grid {name}')
                popkey = (case, setting, level, seed)
                for r in rows:
                    if not math.isfinite(float(r['ns'])) or float(r['ns']) <= 0:
                        raise RuntimeError(f'invalid timing {name}')
                    if populations.setdefault(popkey, r['n']) != r['n']:
                        raise RuntimeError(f'different cardinality {name}')
                    if r['phase'] in ['read', 'read_after_churn']:
                        key = popkey + (r['phase'], r['scope'], r['miss'])
                        answer = tuple(r[k] for k in ['hits', 'keys', 'values'])
                        if answers.setdefault(key, answer) != answer:
                            raise RuntimeError(f'different results {name}: {key}')
                if arm not in ['boost', 'abseil']:
                    for r in rows:
                        state = tuple(r[k] for k in ['bytes','spill','groups','swiss_slots','debt'])
                        identity = popkey + (r['phase'],r['rep'],r['scope'],r['miss'],r['prefetch'])
                        if capacities.setdefault(('state',)+identity,state) != state:
                            raise RuntimeError(f'different physical state {name}: {identity}')
                first = rows[0]
                if arm not in ['boost', 'abseil']:
                    if int(first['ceiling32']) != 28 or float(first['swiss_load']) > 87.501:
                        raise RuntimeError(f'wrong ceiling {name}')
                    if setting == 'spill' and int(first['spill']) != level:
                        raise RuntimeError(f'wrong Swiss population {name}')
                    width = 16
                    spill = int(first['spill'])
                    slots = width if spill else 0
                    while spill * 8 > slots * 7:
                        slots *= 2
                    if int(first['swiss_slots']) != slots:
                        raise RuntimeError(f'unexpected initial Swiss capacity {name}')
                    # Wider minimum allocations differ for very small spill
                    # populations. At larger populations capacities must match.
                    if spill > 56:
                        size = (first['swiss_slots'], first['spill'])
                        if capacities.setdefault(popkey, size) != size:
                            raise RuntimeError(f'unequal initial physical Swiss capacity {name}')
actual = {p.name for p in (root / 'samples').glob('*.csv')}
if expected != actual:
    raise RuntimeError(f'grid difference missing={expected-actual}, extra={actual-expected}')
checks = json.loads((root / 'codegen/comparison.json').read_text())
read_only = any(json.loads((root / f'delta-{v}.json').read_text()).get('read_only',False) for v in owners)
functions = 28 if read_only else 18
if len(checks) != functions * (len(owners) - 1) or not all(c['identical'] for c in checks):
    raise RuntimeError('batch experiment changed primary callers')
for arm in owners:
    if 'directed full logical groups: passed' not in (root / f'check-{arm}.txt').read_text():
        raise RuntimeError(f'missing high-lane check {arm}')
if 'tuple traces passed' not in (root / 'mask-check.txt').read_text():
    raise RuntimeError('missing ordered mask checks')
traces = [(root / f'trace-{arm}.txt').read_text() for arm in owners]
if len(set(traces)) != 1 or '96000 actual lookups' not in traces[0]:
    raise RuntimeError('actual tuple traces differ')
message = f'{len(expected)} callers; complete grid and results match; identical physical state in every phase; all write rounds; exact tuple traces; {len(checks)} identical primary batch loops\n'
(root / 'audit.txt').write_text(message)
print(message, end='')
