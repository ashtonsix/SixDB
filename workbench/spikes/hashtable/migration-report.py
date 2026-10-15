#!/usr/bin/env python3
"""Check the complete migration comparison and preserve per-case losses."""
import argparse
import collections
import csv
import json
import math
import statistics
from pathlib import Path


def summarize(path):
    rows = list(csv.DictReader(path.open()))
    assert rows, 'empty migration result'
    groups = collections.defaultdict(list)
    for row in rows:
        for key in row.keys() - {'arm', 'phase'}:
            row[key] = float(row[key]) if key == 'ns' else int(row[key])
            assert math.isfinite(row[key]) and row[key] >= 0, (key, row[key])
        assert row['operations'] > 0
        groups[tuple(row[k] for k in ('arm', 'K', 'kind', 'A', 'B', 'n', 'phase'))].append(row)
    repeats = sorted({row['rep'] for row in rows})
    assert repeats == list(range(len(repeats)))
    arms = ('migrate8', 'migrate64', 'migrate_all', 'abseil', 'abseil_exact')
    phases = {'construct', 'build', 'prepare', 'migrate', 'finish', 'insert_after',
              'hit_after', 'release', 'lifetime_excluding_reads'}
    expected = set()
    for k in range(8, 65, 8):
        for kind in range(3):
            for a, b in ((3, 1),) if k == 8 else ((3, 1), (9, 5)):
                suffix = max(0, k-a-8)
                row_bytes = 1 + (suffix+7)//8 + (0, 8, 9)[kind]
                slots = next(s for s in (32, 16, 8, 4, 2, 1) if 5+s*row_bytes <= 128)
                n = min((slots << a)*7//8, 128 if k == 8 else 2**32-1)
                serial = 'serial_caller_cycle' if kind == 0 else 'serial_value_cycle'
                for arm in arms:
                    expected.update((arm, k, kind, a, b, n, phase) for phase in phases | {serial})
    assert set(groups) == expected, ('missing/extra cases', expected-set(groups), set(groups)-expected)
    out = []
    for key, samples in sorted(groups.items()):
        assert sorted(r['rep'] for r in samples) == repeats, ('repetition coverage', key)
        assert len({r['operations'] for r in samples}) == 1
        times = [r['ns']/r['operations'] for r in samples]
        result = dict(zip(('arm', 'K', 'kind', 'A', 'B', 'n', 'phase'), key))
        result.update(repetitions=len(repeats), ns_per_operation=statistics.median(times),
                      minimum=min(times), maximum=max(times))
        result.update({name: statistics.median(r[name] for r in samples)
                       for name in ('operations', 'requested', 'owned_capacity', 'metadata', 'steps', 'work')})
        out.append(result)
    lookup = {tuple(r[k] for k in ('arm', 'K', 'kind', 'A', 'B', 'n', 'phase')): r for r in out}
    for r in out:
        for baseline in ('abseil', 'abseil_exact', 'migrate_all'):
            other = lookup[(baseline, *(r[k] for k in ('K', 'kind', 'A', 'B', 'n', 'phase')))]
            if other['ns_per_operation'] > 0:
                r['ratio_to_'+baseline] = r['ns_per_operation']/other['ns_per_operation']
    return out


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = summarize(args.csv)
    if args.output:
        args.output.write_text(json.dumps(results, indent=2)+'\n')
    print(f'Validated {len(results)} arm/case/phase cells; all widths, kinds and repetitions.')
    for r in results:
        if r['K'] == 40 and r['A'] == 9 and r['phase'] in ('migrate', 'hit_after', 'serial_value_cycle', 'lifetime_excluding_reads'):
            print(f"K40 kind={r['kind']} n={r['n']} {r['arm']:12} {r['phase']:25} {r['ns_per_operation']:.3f} ns/op "
                  f"requested={r['requested']} backing={r['owned_capacity']} metadata={r['metadata']}")
