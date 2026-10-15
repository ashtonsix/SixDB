#!/usr/bin/env python3
"""Validate and compare this six-arm native append cohort from retained raw rows.

Usage: compare.py evidence/append-neon evidence/append-zen5 evidence/append-granite
Writes JSON to stdout. Ratios describe medians across fixed cases, not statistical
significance or a workload-weighted performance score.
"""
import csv
import json
import math
import statistics
import sys
from pathlib import Path


def compare(directory):
    raw = list(csv.DictReader((directory / 'formats.csv').open()))
    rows = [{k: v if k == 'phase' else float(v) if k == 'ns' else int(v)
             for k, v in row.items()} for row in raw]
    assert len(rows) == 21870
    axes = ('K', 'kind', 'A', 'phase', 'arm', 'rep')
    indexed = {tuple(r[k] for k in axes): r for r in rows}
    assert len(indexed) == len(rows)
    cases = {(k, kind, a) for k in range(8, 65, 8) for kind in range(3)
             for a in ((6,) if k == 8 else (6, 10))}
    phases = {'construct', 'build', 'hit', 'miss', 'update', 'erase', 'release', 'lifecycle'}
    required = {(k, kind, a, phase, arm, rep) for k, kind, a in cases
                for phase in phases | {'serial_caller_cycle' if kind == 0 else 'serial_value_cycle'}
                for arm in range(6) for rep in range(9)}
    assert indexed.keys() == required
    layout_fields = ('n', 'operations', 'requested', 'owned_capacity', 'metadata',
                     'primary_slots', 'secondary_slots', 'build_primary_rows',
                     'build_secondary_rows', 'build_overflow_rows')
    for key, row in indexed.items():
        assert all(math.isfinite(v) and v >= 0 for k, v in row.items() if k != 'phase')
        if row['arm'] < 4:
            control = indexed[key[:4] + (0, key[-1])]
            assert all(row[f] == control[f] for f in layout_fields), key
            assert sum(row[f] for f in layout_fields[-3:]) == row['n']
        if row['phase'] == 'lifecycle':
            total = sum(indexed[key[:3] + (p,) + key[4:]]['ns']
                        for p in ('construct', 'build', 'update', 'erase', 'release'))
            assert abs(total - row['ns']) < .01
    grouped = {}
    for key in indexed:
        cell = key[:-1]
        if cell not in grouped:
            values = [indexed[cell + (rep,)]['ns'] / indexed[cell + (rep,)]['operations']
                      for rep in range(9)]
            grouped[cell] = (statistics.median(values), min(values), max(values))
    summaries = []
    for kind in range(3):
        for phase in ('build', 'update', 'lifecycle', 'hit', 'miss',
                      'serial_caller_cycle' if kind == 0 else 'serial_value_cycle'):
            for checked, admitted in ((0, 2), (1, 3)):
                ratios, baseline, separated_faster, separated_slower = [], [], 0, 0
                for k, c, a in sorted(cases):
                    if c != kind:
                        continue
                    check = grouped[k, c, a, phase, checked]
                    admit = grouped[k, c, a, phase, admitted]
                    ordinary = grouped[k, c, a, phase, 4]
                    ratios.append(admit[0] / check[0])
                    baseline.append(admit[0] / ordinary[0])
                    separated_faster += admit[2] < check[1]
                    separated_slower += admit[1] > check[2]
                summaries.append(dict(
                    kind=kind, phase=phase, direct=bool(checked), cases=len(ratios),
                    median_ratio_to_checked=statistics.median(ratios),
                    minimum_ratio_to_checked=min(ratios), maximum_ratio_to_checked=max(ratios),
                    medians_below_095=sum(x < .95 for x in ratios),
                    medians_above_105=sum(x > 1.05 for x in ratios),
                    nonoverlapping_ranges_faster=separated_faster,
                    nonoverlapping_ranges_slower=separated_slower,
                    median_ratio_to_abseil=statistics.median(baseline),
                    minimum_ratio_to_abseil=min(baseline), maximum_ratio_to_abseil=max(baseline)))
    return dict(evidence=str(directory), rows=len(rows), cells=len(grouped),
                candidate_memory_population_equal=True, lifecycle_sums_verified=True,
                summaries=summaries)


if __name__ == '__main__':
    assert len(sys.argv) > 1, __doc__
    print(json.dumps([compare(Path(p)) for p in sys.argv[1:]], indent=2))
