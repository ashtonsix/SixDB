#!/usr/bin/env python3
"""Compare executable bucket recipes at identical cardinality and hierarchy geometry."""
import argparse
import collections
import csv
import json
import math
import statistics
from pathlib import Path

NAMES = ['legacy', 'pow2_direct', 'exact', 'exact_direct', 'padded', 'padded_direct',
         'rows', 'rows_direct', 'tag_plane', 'tag_plane_direct', 'tag_bits',
         'tag_bits_direct', 'aligned', 'aligned_direct', 'abseil', 'abseil_exact']
ACCESS_NAMES = ['exact', 'exact_direct', 'wide_fp', 'wide_fp_direct',
                'wide_suffix', 'wide_suffix_direct', 'wide_both', 'wide_both_direct',
                'abseil', 'abseil_exact']
APPEND_NAMES = ['checked', 'checked_direct', 'admitted', 'admitted_direct',
                'abseil', 'abseil_exact']
BAG_NAMES = ['links', 'links_direct', 'buffers', 'buffers_direct', 'abseil', 'abseil_exact']

INLINE_NAMES = ['inline1', 'inline1_direct', 'inline2', 'inline2_direct', 'inline2_roomy', 'inline2_roomy_direct', 'abseil', 'abseil_exact']

def summarize(path, complete=False, access=False, append=False, bags=False, inline=False):
    assert sum((access, append, bags, inline)) <= 1
    names = INLINE_NAMES if inline else BAG_NAMES if bags else APPEND_NAMES if append else ACCESS_NAMES if access else NAMES
    baseline = len(names) - 2
    groups = collections.defaultdict(list)
    for raw in csv.DictReader(path.open()):
        row = {k: v if k == 'phase' else float(v) if k == 'ns' else int(v) for k, v in raw.items()}
        row.setdefault('fanout',0)
        assert all(math.isfinite(v) and v >= 0 for k, v in row.items() if k != 'phase'), row
        assert row['operations'] > 0 and 0 <= row['arm'] < len(names)
        groups[tuple(row[k] for k in ('arm', 'K', 'kind', 'A', 'n', 'fanout', 'phase'))].append(row)
    assert groups
    repetitions = sorted({r['rep'] for rows in groups.values() for r in rows})
    assert repetitions == list(range(len(repetitions))), repetitions
    cases = {(key[1], key[2], key[3], key[4], key[5]) for key in groups}
    if complete:
        required = {(k, kind, a) for k in range(8, 65, 8) for kind in range(3)
                    for a in ((6,) if k == 8 else (6, 10))}
        assert {(k, kind, a) for k, kind, a, n, fanout in cases} == required
    phases = {'construct', 'build', 'hit', 'miss', 'update', 'erase', 'release', 'lifecycle'}
    expected = {(arm, k, kind, a, n, fanout, phase) for k, kind, a, n, fanout in cases for arm in range(len(names))
                for phase in phases | {'serial_caller_cycle' if kind == 0 else 'serial_value_cycle'}}
    assert set(groups) == expected, ('missing/extra cells', expected - groups.keys(), groups.keys() - expected)
    out = []
    for key, rows in sorted(groups.items()):
        assert sorted(r['rep'] for r in rows) == repetitions, key
        assert len({r['operations'] for r in rows}) == 1
        ns = [r['ns'] / r['operations'] for r in rows]
        result = dict(zip(('arm', 'K', 'kind', 'A', 'n', 'fanout', 'phase'), key))
        result.update(name=names[key[0]], repetitions=len(repetitions), ns_per_operation=statistics.median(ns),
                      minimum=min(ns), maximum=max(ns))
        result.update({k: statistics.median(r[k] for r in rows) for k in rows[0]
                       if k not in {'arm', 'K', 'kind', 'A', 'n', 'fanout', 'phase', 'rep', 'ns'}})
        if key[0] < baseline:
            assert result['build_primary_rows'] + result['build_secondary_rows'] + result['build_overflow_rows'] == result['n'], key
        out.append(result)
    lookup = {tuple(r[k] for k in ('arm', 'K', 'kind', 'A', 'n', 'fanout', 'phase')): r for r in out}
    for r in out:
        rest = tuple(r[k] for k in ('K', 'kind', 'A', 'n', 'fanout', 'phase'))
        for arm in (0, baseline, baseline+1):
            denominator = lookup[(arm, *rest)]['ns_per_operation']
            if denominator:
                r['ratio_to_' + names[arm]] = r['ns_per_operation'] / denominator
    return out


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--complete', action='store_true')
    parser.add_argument('--access', action='store_true', help='Same-layout exact/contained-read experiment')
    parser.add_argument('--append', action='store_true', help='Same-owner checked/admitted bag append experiment')
    parser.add_argument('--bags', action='store_true', help='Integrated StreamPack links / Orbital buffers')
    parser.add_argument('--inline', action='store_true', help='One/two-inline multimap storage')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = summarize(args.csv, args.complete, args.access, args.append, args.bags, args.inline)
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    print(f'Validated {len(results)} arm/case/phase cells; all contenders and repetitions present.')
    for r in results:
        if r['K'] in (16, 64) and r['kind'] in (1, 2) and r['phase'] in ('hit', 'serial_value_cycle', 'update', 'lifecycle'):
            print(f"K{r['K']} kind={r['kind']} A{r['A']} {r['name']:14} {r['phase']:20} "
                  f"{r['ns_per_operation']:.3f} ns/op; ordinary Abseil ratio {r.get('ratio_to_abseil', 0):.3f}")
