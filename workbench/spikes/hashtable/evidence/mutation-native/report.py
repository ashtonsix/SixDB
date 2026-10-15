#!/usr/bin/env python3
"""Keep every phase and control in the matched mutation experiment."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import random
from statistics import median

p = argparse.ArgumentParser()
p.add_argument('csv', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--widths', type=int, nargs='+', default=[2, 8, 16])
p.add_argument('--short', action='store_true')
a = p.parse_args()
groups = defaultdict(list)
seen = set()
rows = list(csv.DictReader(a.csv.open()))
phases = {'construct', 'build', 'hit', 'miss', 'serial', 'update', 'erase', 'release', 'lifecycle'}
fans = {2: [1, 2, 4, 5, 8, 9, 16, 32], 8: [1, 2, 3, 4, 8, 32], 16: [1, 2, 3, 8, 32]}
if a.short:
    assert a.widths == [2]
    fans[2] = [1, 4, 5, 8, 9]
variants = ['control', 'fused', 'partial']
schedule = [(w, k, arm, fanout, large) for w in a.widths for k in (1, 2) for arm in range(5)
            for fanout in ([1] if k == 1 else fans[w]) for large in (0, 1)]
random.Random(947).shuffle(schedule)
for r in rows:
    ident = tuple(r[k] for k in ('case', 'order', 'rep', 'phase'))
    assert ident not in seen
    seen.add(ident)
    assert int(r['K']) == 40 and r['phase'] in phases
    case, order = int(r['case']), int(r['order'])
    w, kind, arm, fanout, large = schedule[case]
    assert tuple(int(r[f]) for f in ('value_bytes', 'kind', 'arm', 'fanout', 'A')) == (w, kind, arm, fanout, 10 if large else 6)
    order_variants = variants[case % 3:] + variants[:case % 3]
    assert r['variant'] == (order_variants + list(reversed(order_variants)))[order]
    n = int(r['n'])
    assert n == 6 << int(r['A'])
    assert int(r['operations']) == (1 if r['phase'] in ('construct', 'release') else n//2 if r['phase'] == 'erase'
                                     else n*max(8, 65536//n) if r['phase'] in ('hit', 'miss', 'serial') else n)
    groups[tuple(r[k] for k in ('value_bytes', 'kind', 'A', 'fanout', 'arm', 'phase', 'variant'))].append(r)
reps = {int(r['rep']) for r in rows}
assert reps == set(range(len(reps)))
assert seen == {(str(case), str(order), str(rep), phase) for case in range(len(schedule))
                for order in range(6) for rep in reps for phase in phases}
result = []
inventory = ('requested', 'owned_capacity', 'metadata', 'inline_values', 'abseil_inline_capacity',
             'build_primary_rows', 'build_secondary_rows', 'build_overflow_rows')
ns = lambda batch: median(float(r['ns']) / int(r['operations']) for r in batch)
for key, batch in sorted(groups.items()):
    control = groups[key[:-1]+('control',)]
    fused = groups[key[:-1]+('fused',)]
    partial = groups[key[:-1]+('partial',)]
    assert control and fused and partial and len(batch) == len(control) == len(fused) == len(partial)
    assert len({tuple(r[f] for f in inventory) for r in control+fused+partial}) == 1
    abseil = groups[key[:4]+('3',)+key[5:]]
    exact = groups[key[:4]+('4',)+key[5:]]
    assert abseil and exact
    result.append(dict(zip(('value_bytes', 'kind', 'A', 'fanout', 'arm', 'phase', 'variant'), key)) |
                  {'ns_per_operation': ns(batch), 'over_control': ns(batch)/ns(control),
                   'over_fused': ns(batch)/ns(fused), 'over_abseil': ns(batch)/ns(abseil),
                   'over_exact_abseil': ns(batch)/ns(exact),
                   **{f: int(batch[0][f]) for f in inventory}})
a.output.write_text(json.dumps({'rows': len(rows), 'comparison': result}, indent=2))
with a.output.with_suffix('.csv').open('w') as out:
    w = csv.DictWriter(out, fieldnames=list(result[0]))
    w.writeheader()
    w.writerows(result)
print('Validated', len(rows), 'rows and invariant physical inventories')
for r in result:
    if r['phase'] == 'update' and r['A'] == '10' and r['variant'] == 'partial':
        print('W/kind/fanout/arm', *(r[f] for f in ('value_bytes', 'kind', 'fanout', 'arm')),
              'ns/control/fused/Abseil', *(f'{r[f]:.3f}' for f in ('ns_per_operation', 'over_control', 'over_fused', 'over_abseil')))
