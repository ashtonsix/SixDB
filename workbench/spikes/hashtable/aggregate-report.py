#!/usr/bin/env python3
"""Validate the one-search accumulation comparison; no worker-mode claim."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

ARMS = ('abseil', 'compact', 'fuller', 'fuller_twosearch')
HISTORIES = ('distinct', 'repeated', 'skewed')


def summarize(path, n):
    rows = {}
    for row in csv.DictReader(path.open()):
        arm, history = row['arm'], row['history']
        assert arm in ARMS and history in HISTORIES and int(row['n']) == n
        key = (arm, history, int(row['rep']))
        assert key not in rows
        data = {k: float(v) if k.endswith('_ns') else int(v)
                for k, v in row.items() if k not in ('arm', 'history')}
        assert data['rows'] == n * (1 if history == 'distinct' else 16)
        assert all(math.isfinite(v) and v >= 0 for v in data.values())
        assert all(data[k] > 0 for k in ('setup_ns', 'ingest_ns', 'read_ns', 'release_ns'))
        if arm != 'abseil':
            assert data['primary'] + data['secondary'] + data['chain'] == n
        rows[key] = data
    reps = sorted({k[2] for k in rows})
    assert reps and reps == list(range(len(reps)))
    assert rows.keys() == {(a, h, r) for a in ARMS for h in HISTORIES for r in reps}
    out = []
    for history in HISTORIES:
        for arm in ARMS:
            samples = [rows[arm, history, r] for r in reps]
            med = lambda f: statistics.median(f(x) for x in samples)
            out.append(dict(arm=arm, history=history, n=n, repetitions=len(reps),
                ingest_ns_per_row=med(lambda x: x['ingest_ns']/x['rows']),
                setup_ingest_release_ns_per_row=med(lambda x: (x['setup_ns']+x['ingest_ns']+x['release_ns'])/x['rows']),
                read_ns_per_key=med(lambda x: x['read_ns']/n),
                requested_bytes=med(lambda x: x['requested']),
                owned_capacity_bytes=med(lambda x: x['owned_capacity']),
                owner_metadata_bytes=med(lambda x: x['metadata']),
                primary=med(lambda x: x['primary']),secondary=med(lambda x: x['secondary']),chain=med(lambda x: x['chain']),
                min_ingest_ns_per_row=min(x['ingest_ns']/x['rows'] for x in samples),
                max_ingest_ns_per_row=max(x['ingest_ns']/x['rows'] for x in samples)))
    return dict(input=str(path), rows=len(rows), summaries=out,
        scope='Single-writer fixed-geometry word sums with charged reserve/setup and owner release. '
              'No online-growth, multiwriter or merge conclusion. Owned capacity is not RSS; Abseil backing unknown. '
              'Input rows are 16 bytes each; they are streamed during accumulation.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--n', type=int, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args();result = summarize(args.csv, args.n)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+'\n')
    for h in HISTORIES:
        cells = [x for x in result['summaries'] if x['history'] == h]
        print(h + ': ' + ' '.join(f"{x['arm']} {x['ingest_ns_per_row']:.3f}" for x in cells))
    print(f"Validated {result['rows']} rows; ns per input row, ingestion only above")
