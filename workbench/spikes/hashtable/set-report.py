#!/usr/bin/env python3
"""Validate packed-set membership, mutation, lifetime and memory panels."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

ARMS = ('abseil_set', 'compact', 'fuller')
OPS = ('setup', 'fill', 'hit', 'miss', 'serial_mixed', 'duplicate', 'turnover', 'aged_hit', 'aged_miss', 'release')


def summarize(path, key_bits, n):
    rows = {}
    for raw in csv.DictReader(path.open()):
        a, op = raw['arm'], raw['operation'];assert a in ARMS and op in OPS
        x = {k: float(v) if k == 'ns' else int(v) for k, v in raw.items() if k not in ('arm', 'operation')}
        key = a, op, x['rep'];assert key not in rows and x['K'] == key_bits and x['n'] == n
        assert all(math.isfinite(v) and v >= 0 for v in x.values()) and x['ns'] > 0
        if op in ('setup', 'release'):assert x['operations'] == 1 and x['checksum'] == 0
        elif op in ('fill', 'duplicate', 'turnover'):
            assert x['operations'] == n
            assert x['checksum'] == {'fill': n, 'duplicate': 0, 'turnover': 2*n}[op]
        elif op == 'serial_mixed':assert x['operations'] % (2*n) == 0 and x['checksum']*2 == x['operations']
        else:
            assert x['operations'] % n == 0
            assert x['checksum'] == (0 if op in ('miss', 'aged_miss') else x['operations'])
        if a != 'abseil_set':
            assert x['primary']+x['secondary']+x['chain'] == n
            assert x['slots'] in range(1, 33) and x['unit'] in (48, 64, 96)
        rows[key] = x
    reps = sorted({k[2] for k in rows});assert reps and reps == list(range(len(reps)))
    assert rows.keys() == {(a, op, r) for a in ARMS for op in OPS for r in reps}
    summaries = []
    for op in OPS:
        for a in ARMS:
            sample = [rows[a, op, r] for r in reps]
            med = lambda f: statistics.median(f(x) for x in sample)
            summaries.append(dict(arm=a, operation=op, repetitions=len(reps), K=key_bits, n=n,
                ns_per_operation=med(lambda x: x['ns']/x['operations']),
                min_ns_per_operation=min(x['ns']/x['operations'] for x in sample),
                max_ns_per_operation=max(x['ns']/x['operations'] for x in sample),
                **{k: med(lambda x, k=k: x[k]) for k in ('requested', 'owned_capacity', 'metadata',
                    'primary', 'secondary', 'chain', 'A', 'B', 'slots', 'unit')}))
    lifetime = {a: statistics.median(sum(rows[a, op, r]['ns'] for op in ('setup', 'fill', 'release'))/n
                                  for r in reps) for a in ARMS}
    return dict(input=str(path), rows=len(rows), summaries=summaries, construction_release_ns_per_key=lifetime,
        scope='Zero payload in primary, secondary and StreamPack chains. Fixed geometry and charged baseline reserve. '
              'Serial membership includes a dependent 24B query-source node and equal hit/miss cycles. '
              'Turnover is one erase+insert pair per operation; aged-hit follows one disjoint turnover. '
              'Release follows all mutation panels; shared runtime teardown/preparation not included. '
              'Requested/backing/metadata counters are not per-table RSS. Native-word K40 baseline is not exact-byte. '
              'Working-set labels do not assert a cache residence.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__);p.add_argument('csv', type=Path)
    p.add_argument('--key-bits', type=int, required=True);p.add_argument('--n', type=int, required=True)
    p.add_argument('--output', type=Path);args = p.parse_args();out = summarize(args.csv, args.key_bits, args.n)
    if args.output:args.output.write_text(json.dumps(out, indent=2)+'\n')
    for op in OPS:
        print(op+': '+' '.join(f"{x['arm']} {x['ns_per_operation']:.3f}" for x in out['summaries'] if x['operation'] == op))
    print(f"Validated {out['rows']} rows")
