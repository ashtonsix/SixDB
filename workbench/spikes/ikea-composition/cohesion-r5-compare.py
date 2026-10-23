#!/usr/bin/env python3
"""Paired before/after medians for the cohesion native and peripheral cohorts.

Each case pairs trial t of `before` with trial t of `after` (arms alternate
order per trial). Reports the median of per-trial after/before time ratios,
so >1 means the after arm is slower. Prints every case beyond --threshold.
"""
import argparse
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def google(path):
    out = {}
    for b in json.loads(path.read_text())['benchmarks']:
        if b.get('run_type') != 'iteration':
            continue
        items = b.get('items_per_second')
        out[b['name']] = 1e9 / items if items else b['real_time']
    return out


def table(path, keys, value):
    out = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            out['/'.join(row[k] for k in keys)] = float(row[value])
    return out


def load(arm_dir, trial):
    cases = {}
    def add(family, values):
        for k, v in values.items():
            cases[family + ':' + k] = v
    for family in ['seriespack', 'tuplepack', 'initialize', 'bec']:
        p = arm_dir / f'{family}-trial-{trial}.json'
        if p.exists():
            add(family, google(p))
    p = arm_dir / f'filters-trial-{trial}.csv'
    if p.exists():
        add('filters', table(p, ['workload', 'width_or_cases', 'op', 'active', 'candidate', 'metric'],
                             'ns_window'))
    for p in sorted(arm_dir.glob(f'htable*-trial-{trial}.csv')):
        family = p.name.split('-trial-')[0]
        add(family, table(p, ['phase', 'miss', 'new_percent'], 'ns'))
    for p in sorted(arm_dir.glob(f'stream-*-trial-{trial}.csv')):
        family = p.name.split('-trial-')[0]
        add(family, table(p, ['family', 'path', 'operation'], 'ns_per_value'))
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('results', nargs='+', type=Path, help='worker results directories')
    ap.add_argument('--threshold', type=float, default=0.03)
    ap.add_argument('--trials', type=int, default=5)
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    rows = []
    for results in a.results:
        for profile_dir in sorted(p for p in results.iterdir() if (p / 'before').is_dir()):
            ratios = defaultdict(list)
            before_t, after_t = defaultdict(list), defaultdict(list)
            for t in range(a.trials):
                b, f = load(profile_dir / 'before', t), load(profile_dir / 'after', t)
                for k in b.keys() & f.keys():
                    ratios[k].append(f[k] / b[k])
                    before_t[k].append(b[k])
                    after_t[k].append(f[k])
            for k, r in ratios.items():
                rows.append({'profile': profile_dir.name, 'case': k, 'ratio': statistics.median(r),
                             'min_ratio': min(r), 'max_ratio': max(r),
                             'before': statistics.median(before_t[k]),
                             'after': statistics.median(after_t[k]), 'n': len(r)})
    rows.sort(key=lambda r: -r['ratio'])
    by = defaultdict(list)
    for r in rows:
        by[(r['profile'], r['case'].split(':')[0])].append(r['ratio'])
    print('profile  family      cases  median  worst   best   >+3%  <-3%')
    for (p, fam), rs in sorted(by.items()):
        print(f'{p:8} {fam:11} {len(rs):5}  {statistics.median(rs):.3f}  {max(rs):.3f}  {min(rs):.3f}'
              f'  {sum(x > 1.03 for x in rs):4}  {sum(x < 0.97 for x in rs):4}')
    print()
    for r in rows:
        if r['ratio'] > 1 + a.threshold:
            print(f"{r['profile']:7} {r['ratio']:.3f} [{r['min_ratio']:.3f},{r['max_ratio']:.3f}] "
                  f"{r['before']:.4g}->{r['after']:.4g} {r['case']}")
    if a.json:
        a.json.write_text(json.dumps(rows, indent=1) + '\n')


if __name__ == '__main__':
    sys.exit(main())
