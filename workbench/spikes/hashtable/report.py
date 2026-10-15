#!/usr/bin/env python3
"""Validate and summarize the bounded first comparison, retaining every arm."""
import argparse
import collections
import csv
import json
import math
import statistics
from pathlib import Path

ARMS = ('abseil', 'compact', 'fuller', 'fuller_direct')
FILTERS = ('compact_filter', 'fuller_filter')
PHASES = ('build', 'dependent', 'independent_hit', 'independent_half_miss',
          'update', 'turnover', 'aged_dependent', 'owner_release')
PACKETS = ('cohort_dependent', 'packet4_hit', 'packet4_half_miss', 'packet8_hit', 'packet8_half_miss')
CASES = ((16, 270), (16, 5000), (40, 1800))
PANEL = CASES + ((16, 180), (16, 330), (16, 3500), (16, 6000), (40, 1600), (40, 2100))
RESIDENCE = ((40, 110000), (40, 3600000))


def summarize(path, cases=CASES, filters=False, packets=False, residence=False):
    arms = lambda k: (ARMS[:3] if residence else ARMS) + (FILTERS if filters and k == 40 else ())
    phases = PHASES + (PACKETS if packets else ())
    timing, counters, preparation = {}, {}, []
    for row in csv.reader(path.read_text().splitlines()):
        if not row or row[0].startswith('#'):
            continue
        kind = row[0]
        if kind == 'time':
            _, arm, k, rep, phase, n, ops, ns = row
            key = (int(k), int(n), arm, phase, int(rep))
            assert key not in timing, ('duplicate timing', key)
            assert arm in arms(int(k)) and phase in phases and int(ops) > 0
            assert math.isfinite(float(ns)) and float(ns) > 0
            timing[key] = float(ns)
        elif kind in ('memory', 'aged_memory', 'route', 'aged_route', 'filter', 'aged_filter'):
            _, arm, k, rep, n, a, b, c = row
            key = (int(k), int(n), arm, kind, int(rep))
            assert key not in counters, ('duplicate accounting', key)
            counters[key] = tuple(map(int, (a, b, c)))
        elif kind == 'preparation':
            preparation.append(row[1:])
        elif kind == 'geometry':
            assert len(row) == 13, row
        else:
            raise AssertionError(('unknown record', row))
    reps = sorted({key[-1] for key in timing})
    assert reps == list(range(len(reps))) and reps, reps
    expected = {(k, n, arm, phase, rep) for k, n in cases for arm in arms(k)
                for phase in phases for rep in reps}
    assert timing.keys() == expected, ('timing coverage', expected - timing.keys(), timing.keys() - expected)
    summaries = []
    for k, n in cases:
        for arm in arms(k):
            for kind in ('memory', 'aged_memory'):
                for rep in reps:
                    assert (k, n, arm, kind, rep) in counters
            if arm != 'abseil':
                for kind in ('route', 'aged_route'):
                    for rep in reps:
                        assert sum(counters[k, n, arm, kind, rep]) == n, 'tier cardinality'
            if arm in FILTERS:
                for kind in ('filter', 'aged_filter'):
                    for rep in reps:
                        bits, stale, avoided = counters[k, n, arm, kind, rep]
                        assert 0 <= stale <= bits and 0 <= avoided <= n
            for phase in phases:
                values = [timing[k, n, arm, phase, rep] for rep in reps]
                paired = [timing[k, n, 'abseil', phase, rep] / timing[k, n, arm, phase, rep]
                          for rep in reps]
                summaries.append(dict(k=k, n=n, arm=arm, phase=phase, repetitions=len(reps),
                                      ns=statistics.median(values), min_ns=min(values), max_ns=max(values),
                                      baseline_over_candidate=statistics.median(paired)))
    accounting = []
    for k, n in cases:
        for arm in arms(k):
            for kind in ('memory', 'aged_memory', 'route', 'aged_route', 'filter', 'aged_filter'):
                samples = [v for key, v in counters.items() if key[:4] == (k, n, arm, kind)]
                if samples:
                    accounting.append(dict(k=k, n=n, arm=arm, kind=kind,
                        median=[statistics.median(v[i] for v in samples) for i in range(3)],
                        minimum=[min(v[i] for v in samples) for i in range(3)],
                        maximum=[max(v[i] for v in samples) for i in range(3)]))
    return dict(input=str(path), timing_rows=len(timing), timings=summaries,
                accounting=accounting, preparation=preparation)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--panel', action='store_true')
    parser.add_argument('--filters', action='store_true')
    parser.add_argument('--packets', action='store_true')
    parser.add_argument('--residence', action='store_true')
    args = parser.parse_args()
    assert not (args.residence and (args.panel or args.filters))
    cases = RESIDENCE if args.residence else PANEL if args.panel else CASES
    result = summarize(args.csv, cases, args.filters, args.packets, args.residence)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    for k, n in cases:
        print(f'K{k} / {n} entries; median ns/operation; owner_release excludes pool drain')
        for phase in PHASES + (PACKETS if args.packets else ()):
            cells = {x['arm']: x['ns'] for x in result['timings']
                     if (x['k'], x['n'], x['phase']) == (k, n, phase)}
            shown = (ARMS[:3] if args.residence else ARMS) + (FILTERS if args.filters and k == 40 else ())
            print(f'  {phase:24s}' + ' '.join(f'{arm} {cells[arm]:8.3f}' for arm in shown))
    print(f"Validated {result['timing_rows']} timing rows with fresh/aged accounting")


if __name__ == '__main__':
    main()
