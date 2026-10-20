#!/usr/bin/env python3
"""Offline equal-budget scorecard; retain native and over-budget controls."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
args = parser.parse_args()
root = args.root
plans = json.loads((root / 'plan.json').read_text())
schedule = json.loads((root / 'schedule.json').read_text())
assert len({(p['name'], p['v']) for p in plans}) == 10
plan_by_key = {(p['name'], p['v']): p for p in plans}
groups = collections.defaultdict(list)
states = collections.defaultdict(list)
answers = {}
rows_seen = 0
executions = collections.Counter()
binary_hashes = {}

def add(record, phase, miss, prefetch, ns, operations, rows):
    groups[(record['case'], record['w'], record['v'], record['arm'],
            phase, miss, prefetch)].append(dict(ns=ns, operations=operations,
                bytes=max(int(r['bytes']) for r in rows),
                peak=max(int(r['peak_bytes']) for r in rows),
                seed=record['seed'], trial=record['trial']))

for record in schedule:
    path = root / 'samples' / record['file']
    receipt = json.loads(path.with_suffix('.json').read_text())
    assert receipt['exit'] == 0
    binary = root.parent / 'binaries' / Path(receipt['command'][3]).name
    if binary not in binary_hashes:
        binary_hashes[binary] = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert binary_hashes[binary] == receipt['binary_sha256']
    rows = list(csv.DictReader(path.open()))
    assert receipt['rows'] == len(rows)
    assert {int(r['n']) for r in rows} == {record['n']}
    assert {int(r['seed']) for r in rows} == {record['seed']}
    assert {int(r['v']) for r in rows} == {record['v']}
    assert {int(r['k']) + int(r['b']) for r in rows} == {record['w']}
    assert all(0 < float(r['ns']) and int(r['peak_bytes']) >= int(r['bytes']) for r in rows)
    rows_seen += len(rows)
    build, = [r for r in rows if r['phase'] == 'build']
    assert int(build['operations']) == record['n']
    assert len([r for r in rows if r['phase'] == 'refill']) == 7
    executions[(record['case'], record['v'], record['arm'])] += 1
    p = plan_by_key[(record['case'], record['v'])]
    arm = next(a for a in p['arms'] if a['name'] == record['arm'])
    # All selected peers fit this declared usable ceiling; upper Ikea references
    # remain explicitly over-budget instead of being mixed into the main result.
    ceiling = max(int(a['row']['bytes']) for a in p['arms'] if a['impl'] != 'ikea')
    states[(record['case'], record['w'], record['v'], record['arm'])].append(dict(
        seed=record['seed'], trial=record['trial'], b=record['b'],
        reservation=arm['reservation'], ceiling=ceiling,
        census_bytes=int(arm['row']['bytes']), initial_bytes=int(build['bytes']),
        slots=int(build['slots']), initial_load=float(build['load']),
        initial_spill=int(build['spill']),
        max_retained=max(int(r['bytes']) for r in rows),
        max_peak=max(int(r['peak_bytes']) for r in rows)))
    for r in rows:
        phase = r['phase']
        if phase == 'erase' or phase == 'refill':
            continue
        miss, prefetch = int(r['miss']), int(r['prefetch'])
        if phase.startswith('read'):
            answer = tuple(int(r[f]) for f in ['hits', 'keys', 'values'])
            key = (record['case'], record['v'], record['seed'], phase, miss)
            assert answers.setdefault(key, answer) == answer, (record, key, answer)
            if miss == 0:
                assert answer[0] == int(r['operations'])
            if miss == 100:
                assert answer == (0, 0, 0)
        add(record, phase, miss, prefetch, float(r['ns']), int(r['operations']), [r])
    refills = [r for r in rows if r['phase'] == 'refill']
    erases = [r for r in rows if r['phase'] == 'erase' and int(r['rep']) >= 3]
    assert len(refills) == len(erases) == 7
    count = sum(int(r['operations']) for r in refills)
    assert sum(int(r['operations']) for r in erases) == count
    for phase, selected in [('refill_amortized', refills), ('erase_amortized', erases),
                            ('erase_refill_pair', refills + erases)]:
        ns = sum(float(r['ns']) * int(r['operations']) for r in selected) / count
        add(record, phase, 0, 0, ns, count, selected)

for p in plans:
    for arm in p['arms']:
        assert executions[(p['name'], p['v'], arm['name'])] == (
            6 if p['name'].startswith('cold') else 15)

with (root / 'summary.csv').open('w') as f:
    fields = ['case', 'w', 'v', 'arm', 'phase', 'miss', 'prefetch', 'ns', 'mad',
              'mops', 'samples', 'seeds', 'bytes_median', 'peak_bytes_max']
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    for key, rr in sorted(groups.items()):
        ns = statistics.median(r['ns'] for r in rr)
        writer.writerow(dict(zip(fields[:7], key), ns=ns,
            mad=statistics.median(abs(r['ns'] - ns) for r in rr), mops=1000 / ns,
            samples=len(rr), seeds=len({r['seed'] for r in rr}),
            bytes_median=statistics.median(r['bytes'] for r in rr),
            peak_bytes_max=max(r['peak'] for r in rr)))
with (root / 'memory.csv').open('w') as f:
    fields = ['case', 'w', 'v', 'arm', 'b', 'reservation', 'ceiling', 'census_bytes',
              'initial_bytes_median', 'initial_bytes_max', 'slots', 'initial_load',
              'initial_spill_median', 'max_retained', 'max_peak', 'initial_over_ceiling',
              'peak_over_ceiling']
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    for key, rr in sorted(states.items()):
        first = rr[0]
        row = dict(zip(fields[:4], key))
        row.update({k: first[k] for k in ['b', 'reservation', 'ceiling', 'census_bytes', 'slots']})
        row.update(initial_bytes_median=statistics.median(r['initial_bytes'] for r in rr),
                   initial_bytes_max=max(r['initial_bytes'] for r in rr),
                   initial_load=statistics.median(r['initial_load'] for r in rr),
                   initial_spill_median=statistics.median(r['initial_spill'] for r in rr),
                   max_retained=max(r['max_retained'] for r in rr),
                   max_peak=max(r['max_peak'] for r in rr))
        row['initial_over_ceiling'] = row['initial_bytes_max'] / first['ceiling']
        row['peak_over_ceiling'] = row['max_peak'] / first['ceiling']
        writer.writerow(row)
(root / 'audit.json').write_text(json.dumps(dict(processes=len(schedule), rows=rows_seen,
    surfaces=len(plans), cross_arm_answers=len(answers),
    verified_binaries=len(binary_hashes), native_and_budget_arms_retained=True,
    seven_refill_rounds_amortized=True), indent=2) + '\n')
print((root / 'audit.json').read_text())
