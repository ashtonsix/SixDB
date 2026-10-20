#!/usr/bin/env python3
"""Audit the paired cold-write follow-up, including every maintenance round."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics

p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
a = p.parse_args()
records = json.loads((a.root / 'runs.json').read_text())
assert len(records) == 120
groups = collections.defaultdict(list)
answers = {}
counts = collections.Counter()
rows_seen = 0
binary_hashes = {}
for record in records:
    assert record['exit'] == 0
    binary = a.root.parent / 'binaries' / Path(record['command'][3]).name
    if binary not in binary_hashes:
        binary_hashes[binary] = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert binary_hashes[binary] == record['binary_sha256']
    rows = list(csv.DictReader((a.root / record['file']).open()))
    rows_seen += len(rows)
    counts[(record['w'], record['v'], record['impl'], record['prefetch'])] += 1
    assert {int(r['n']) for r in rows} == {record['n']}
    assert {int(r['seed']) for r in rows} == {record['seed']}
    assert all(int(r['peak_bytes']) >= int(r['bytes']) for r in rows)
    selected = []
    for row in rows:
        if row['phase'].startswith('read'):
            key = (record['w'], record['v'], record['seed'], row['phase'], row['miss'])
            answer = tuple(int(row[k]) for k in ['hits', 'keys', 'values'])
            assert answers.setdefault(key, answer) == answer
        elif row['phase'] not in ['erase', 'refill']:
            assert int(row['prefetch']) == record['prefetch']
            selected.append((row['phase'], float(row['ns']), [row]))
    refills = [r for r in rows if r['phase'] == 'refill']
    erases = [r for r in rows if r['phase'] == 'erase' and int(r['rep']) >= 3]
    assert len(refills) == len(erases) == 7
    count = sum(int(r['operations']) for r in refills)
    assert sum(int(r['operations']) for r in erases) == count
    for phase, rr in [('refill_amortized', refills), ('erase_amortized', erases),
                      ('erase_refill_pair', refills + erases)]:
        selected.append((phase, sum(float(r['ns']) * int(r['operations']) for r in rr) / count, rr))
    half_erase, = [r for r in rows if r['phase'] == 'erase' and int(r['rep']) == 2]
    upsert, = [r for r in rows if r['phase'] == 'upsert50']
    selected.append(('upsert50_with_erase', float(upsert['ns']) + float(half_erase['ns']) *
                     int(half_erase['operations']) / int(upsert['operations']), [upsert, half_erase]))
    for phase, ns, rr in selected:
        groups[(record['w'], record['v'], record['impl'], record['prefetch'], phase)].append(
            dict(ns=ns, bytes=max(int(r['bytes']) for r in rr),
                 peak=max(int(r['peak_bytes']) for r in rr), seed=record['seed']))
assert len(counts) == 20 and set(counts.values()) == {6}
with (a.root / 'summary.csv').open('w') as f:
    fields = ['w', 'v', 'impl', 'prefetch', 'phase', 'ns', 'mad', 'mops',
              'samples', 'seeds', 'bytes_median', 'peak_bytes_max']
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    for key, rr in sorted(groups.items()):
        ns = statistics.median(r['ns'] for r in rr)
        writer.writerow(dict(zip(fields[:5], key), ns=ns,
            mad=statistics.median(abs(r['ns'] - ns) for r in rr), mops=1000 / ns,
            samples=len(rr), seeds=len({r['seed'] for r in rr}),
            bytes_median=statistics.median(r['bytes'] for r in rr),
            peak_bytes_max=max(r['peak'] for r in rr)))
(a.root / 'audit.json').write_text(json.dumps(dict(processes=len(records), rows=rows_seen,
    surfaces=4, cross_arm_answers=len(answers), verified_binaries=len(binary_hashes),
    seven_refill_rounds_amortized=True), indent=2) + '\n')
print((a.root / 'audit.json').read_text())
