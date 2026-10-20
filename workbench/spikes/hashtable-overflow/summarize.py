#!/usr/bin/env python3
import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('input', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
files = sorted((a.input / 'samples').glob('*.csv'))
rows = []
for path in files:
    current = list(csv.DictReader(path.open()))
    if not current:
        raise RuntimeError(f'empty {path}')
    for r in current:
        if not math.isfinite(float(r['ns'])) or float(r['ns']) <= 0:
            raise RuntimeError(f'invalid timing {path}')
    rows.extend(current)
if not rows:
    raise RuntimeError('no measurements')
with (a.output / 'samples.csv').open('w') as out:
    writer = csv.DictWriter(out, fieldnames=list(rows[0]))
    writer.writeheader(); writer.writerows(rows)
for r in rows:
    for key in ['ceiling32','target_spill','swiss_slots','debt','swiss_load']:
        r.setdefault(key, '0')
keys = ['cpu', 'isa', 'k', 'b', 'v', 'e', 'load', 'target_spill', 'phase', 'scope', 'miss', 'prefetch', 'conditioning']
groups = defaultdict(list)
for row in rows:
    groups[tuple(row[k] for k in keys) + (row['impl'],)].append(row)
medians = {k: statistics.median(float(r['ns']) for r in rs) for k, rs in groups.items()}
fields = keys + ['impl', 'ns', 'mad', 'samples', 'seeds', 'bytes', 'spill', 'n', 'ceiling32', 'swiss_slots', 'debt', 'swiss_load', 'vs_baseline', 'vs_abseil', 'vs_boost']
with (a.output / 'cases.csv').open('w') as out:
    writer = csv.DictWriter(out, fieldnames=fields); writer.writeheader()
    for key, rs in sorted(groups.items()):
        result = dict(zip(keys + ['impl'], key))
        ns = medians[key]
        result.update(ns=ns, mad=statistics.median(abs(float(r['ns']) - ns) for r in rs),
                      samples=len(rs), seeds=len({r['seed'] for r in rs}),
                      bytes=statistics.median(int(r['bytes']) for r in rs),
                      spill=statistics.median(int(r['spill']) for r in rs), n=rs[0]['n'])
        for field in ['ceiling32','swiss_slots','debt','swiss_load']:
            result[field] = statistics.median(float(r[field]) for r in rs)
        for arm in ['baseline', 'abseil', 'boost']:
            other = key[:-1] + (arm,)
            # Boost has no public prefetch-start hook. Its native caller is the
            # comparator for both direct and software-lookahead panels.
            if arm == 'boost' and other not in medians:
                fixed = list(key[:-1]); fixed[keys.index('prefetch')] = '0'
                other = tuple(fixed) + (arm,)
            result['vs_' + arm] = ns / medians[other] if other in medians else ''
        writer.writerow(result)
# Include every pure-churn maintenance pulse; a median round can hide rehashing.
pairs = defaultdict(list)
pair_keys = ['cpu', 'isa', 'k', 'b', 'v', 'load', 'target_spill', 'impl']
for path in files:
    rs = list(csv.DictReader(path.open()))
    for r in rs:
        r.setdefault('target_spill', '0')
    churn = [r for r in rs if r['phase'] in ['erase', 'refill'] and int(r['rep']) >= 3]
    count = sum(int(r['operations']) for r in churn if r['phase'] == 'refill')
    if count:
        total = sum(float(r['ns']) * int(r['operations']) for r in churn)
        pairs[tuple(rs[0][k] for k in pair_keys)].append((total / count, rs[0]['seed']))
with (a.output / 'churn.csv').open('w') as out:
    writer = csv.DictWriter(out, fieldnames=pair_keys + ['ns_per_pair', 'mad', 'seeds'])
    writer.writeheader()
    for key, measurements in sorted(pairs.items()):
        median = statistics.median(ns for ns, _ in measurements)
        result = dict(zip(pair_keys, key))
        result.update(ns_per_pair=median,
                      mad=statistics.median(abs(ns - median) for ns, _ in measurements),
                      seeds=len({seed for _, seed in measurements}))
        writer.writerow(result)
print(f'{len(files)} callers, {len(rows)} samples, {len(groups)} summary cells')
