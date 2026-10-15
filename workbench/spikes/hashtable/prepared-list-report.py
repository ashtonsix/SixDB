#!/usr/bin/env python3
"""Validate paired complete lifetimes; retain unknown memory quantities as -1."""
import argparse
import csv
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('root', type=Path)
a = p.parse_args()
groups = {}
files = sorted(a.root.glob('hashtable_design_prepared_list_*.csv'))
assert files, 'no prepared-list timings'
for path in files:
    rows = list(csv.DictReader(path.open()))
    assert rows, path
    keys = {int(r['K']) for r in rows}
    assert len(keys) == 1 and keys <= set(range(8, 65, 8))
    reps = {int(r['rep']) for r in rows}
    assert reps == set(range(len(reps)))
    expected = {(str(kind), str(w), arm, str(n), str(grain), str(rep))
                for kind, w in [(0, 0), (1, 2), (1, 64), (2, 2), (2, 64)]
                for arm in ['synchronous', 'prepared', 'abseil', 'abseil_exact']
                for n in [16, 128, 240] for grain in [8, 64, 256] for rep in reps}
    seen = set()
    for row in rows:
        cell = tuple(row[f] for f in ['kind', 'W', 'arm', 'keys', 'grain', 'rep'])
        assert cell not in seen
        seen.add(cell)
        assert float(row['lifetime_ns']) > 0 and int(row['histories']) > 0
        if row['arm'] == 'synchronous':
            assert int(row['coexist_logical']) == int(row['coexist_backing']) == -1
        if row['arm'].startswith('abseil'):
            assert int(row['after_backing']) == int(row['coexist_backing']) == -1
        if row['arm'] == 'prepared':
            assert int(row['coexist_logical']) > int(row['after_logical'])
        control = 0 if '_checked' in path.stem else 1
        key = (control,) + tuple(row[f] for f in ['K', 'kind', 'W', 'arm', 'keys', 'grain'])
        groups.setdefault(key, []).append(row)
    assert seen == expected, (path, seen ^ expected)

summary = []
for key, batch in sorted(groups.items()):
    row = dict(zip(['unique_promotion', 'K', 'kind', 'W', 'arm', 'keys', 'grain'], key))
    for field in ['histories', 'before_logical', 'after_logical', 'coexist_logical', 'digest']:
        values = {int(r[field]) for r in batch}
        assert len(values) == 1, (key, field, values)
        row[field] = values.pop()
    # Pool reuse can choose different physical extents for identical logical
    # contents. Preserve that variation rather than treating it as lost coverage.
    for field in ['after_backing', 'coexist_backing']:
        values = [int(r[field]) for r in batch]
        assert all(v == -1 for v in values) or all(v >= 0 for v in values)
        row[field] = statistics.median(values)
        row[field + '_min'] = min(values)
        row[field + '_max'] = max(values)
    times = [float(r['lifetime_ns']) for r in batch]
    row.update(samples=len(times), lifetime_ns=statistics.median(times),
               min_ns=min(times), max_ns=max(times))
    summary.append(row)
with (a.root / 'summary.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
print(f'{len(files)} files, {sum(len(v) for v in groups.values())} raw rows, '
      f'{len(summary)} complete lifetime/storage cases')
