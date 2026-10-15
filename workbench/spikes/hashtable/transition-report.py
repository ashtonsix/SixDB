#!/usr/bin/env python3
"""Validate the single-Lane geometry transition and its consuming baseline."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

ARMS = ('abseil_natural', 'abseil_reserve', 'rebuild', 'unique')
OVERLAPS = (0, 50, 90)
TRANSITION = ('prepare_ns', 'migrate_ns', 'old_retire_ns', 'merge_ns', 'source_retire_ns')
LIFECYCLE = ('setup_ns', 'build_ns', *TRANSITION, 'release_ns')


def summarize(path, local):
    rows = {}
    for raw in csv.DictReader(path.open()):
        arm = raw['arm'];assert arm in ARMS
        x = {k: float(v) if k.endswith('_ns') else int(v) for k, v in raw.items() if k != 'arm'}
        key = arm, x['overlap'], x['rep'];assert key not in rows and x['overlap'] in OVERLAPS
        assert x['local'] == local and x['union'] == 2*local-local*x['overlap']//100
        assert x['added'] == x['union']-local
        assert all(math.isfinite(v) and v >= 0 for v in x.values())
        assert all(x[k] > 0 for k in ('setup_ns', 'build_ns', 'merge_ns', 'read_ns', 'release_ns'))
        assert x['grown'] in (0, 1)
        assert x['after_requested'] >= x['retained_requested'] > 0
        if arm.startswith('abseil'):
            assert not any(x[k] for k in ('migrated', 'primary', 'secondary', 'chain', 'migrate_ns', 'old_retire_ns'))
            assert x['baseline_peak_requested'] >= max(x[k] for k in ('before_requested', 'staged_requested', 'after_requested'))
        else:
            assert x['grown'] == (x['overlap'] != 90)
            assert x['migrated'] == local*x['grown'] and x['baseline_peak_requested'] == 0
            assert x['primary']+x['secondary']+x['chain'] == x['union']
            if x['grown']:assert all(x[k] > 0 for k in ('prepare_ns', 'migrate_ns', 'old_retire_ns'))
            else:assert all(x[k] == 0 for k in ('prepare_ns', 'migrate_ns', 'old_retire_ns'))
        rows[key] = x
    reps = sorted({k[2] for k in rows});assert reps and reps == list(range(len(reps)))
    assert rows.keys() == {(a, h, r) for a in ARMS for h in OVERLAPS for r in reps}
    for h in OVERLAPS:
        for r in reps:
            for k in ('before_requested', 'staged_requested', 'after_requested', 'retained_requested', 'primary', 'secondary', 'chain'):
                assert rows['rebuild', h, r][k] == rows['unique', h, r][k], (h, r, k)
    out = []
    for h in OVERLAPS:
        for arm in ARMS:
            sample = [rows[arm, h, r] for r in reps]
            med = lambda f: statistics.median(f(x) for x in sample)
            total = lambda x, fields: sum(x[k] for k in fields)
            out.append(dict(arm=arm, overlap=h, local=local, union=sample[0]['union'], repetitions=len(reps),
                grown=sample[0]['grown'],
                transition_ns_per_key=med(lambda x: total(x, TRANSITION)/x['union']),
                lifecycle_ns_per_input=med(lambda x: total(x, LIFECYCLE)/(2*local)),
                read_ns_per_key=med(lambda x: x['read_ns']/x['union']),
                **{k: med(lambda x, k=k: x[k]) for k in ('prepare_ns', 'migrate_ns', 'merge_ns',
                    'before_requested', 'staged_requested', 'after_requested', 'retained_requested',
                    'retained_owned', 'metadata', 'baseline_peak_requested', 'primary', 'secondary', 'chain')},
                candidate_sample_max=0 if arm.startswith('abseil') else med(lambda x: max(x[k] for k in
                    ('before_requested', 'staged_requested', 'after_requested'))),
                min_transition_ns_per_key=min(total(x, TRANSITION)/x['union'] for x in sample),
                max_transition_ns_per_key=max(total(x, TRANSITION)/x['union'] for x in sample)))
    return dict(input=str(path), rows=len(rows), summaries=out,
        scope='One Lane, actual local construction, fresh-seed private migration and consuming merge. '
              'Exact union occupancy supplied equally; provisional 7/8 trigger. Warm reads after source retirement. '
              'Abseil peak is requested allocator high-water; candidate is sampled coexistence, not exact transient peak. '
              'No worker scaling, online ingestion growth, retained MVCC reader or per-table RSS claim.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__);p.add_argument('csv', type=Path)
    p.add_argument('--local', type=int, required=True);p.add_argument('--output', type=Path)
    args = p.parse_args();out = summarize(args.csv, args.local)
    if args.output:args.output.write_text(json.dumps(out, indent=2)+'\n')
    for h in OVERLAPS:
        print(str(h)+'% overlap: '+' '.join(f"{x['arm']} {x['transition_ns_per_key']:.3f}"
              for x in out['summaries'] if x['overlap'] == h))
    print(f"Validated {out['rows']} rows; transition ns/union key above")
