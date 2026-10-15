#!/usr/bin/env python3
"""Matched full-value / final-eight-byte consumers with complete table histories."""
import argparse
from collections import defaultdict
import csv
import hashlib
import io
import json
from pathlib import Path
import random
import shutil
from statistics import median
import subprocess
import time

PHASES = ('construct', 'build', 'hit', 'miss', 'update', 'aged_hit',
          'erase', 'reuse', 'release', 'lifecycle')
INVENTORY = ('operations', 'requested', 'metadata', 'primary', 'secondary', 'overflow')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def csv_write(path, rows):
    with path.open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def report(out):
    receipt = json.loads((out / 'processes.json').read_text())
    rows = list(csv.DictReader((out / 'values.csv').open()))
    for name, digest in receipt['binary_sha256'].items():
        assert sha(out / name) == digest
    processes = receipt['processes']
    expected = {(c, n, a, w, v, p) for c in receipt['cases'] for n in receipt['counts']
                for a in range(4) for w in receipt['widths'] for v in (0, 1) for p in (0, 1)}
    assert len(processes) == len(expected)
    assert {(p['case'], p['n'], p['arm'], p['width'], p['consumer'], p['pass_index'])
            for p in processes} == expected
    assert len(rows) == len(processes) * receipt['reps'] * len(PHASES)
    groups = defaultdict(list)
    for i, process in enumerate(processes):
        assert process['returncode'] == 0 and not process['stderr']
        if i:
            assert process['start'] >= processes[i - 1]['end']
        c, n, a, w, v, p = (process[k] for k in ('case', 'n', 'arm', 'width', 'consumer', 'pass_index'))
        kwk = list(map(int, c.split('_')))
        begin = i * receipt['reps'] * len(PHASES)
        for j, row in enumerate(rows[begin:begin + receipt['reps'] * len(PHASES)]):
            assert [int(row[k]) for k in ('K', 'W', 'kind')] == kwk
            assert row['case'] == c
            assert [int(row[k]) for k in ('n', 'arm', 'N', 'consumer', 'pass_index', 'rep')] == [n, a, w, v, p, j // len(PHASES)]
            assert row['phase'] == PHASES[j % len(PHASES)]
            assert int(row['operations']) > 0 and float(row['ns']) >= 0
            groups[c, n, a, w, v, row['phase']].append(row)

    def inventory(batch):
        return sorted(tuple(int(row[k]) for k in INVENTORY) for row in batch)

    def cost(batch, pass_index=None):
        return median(float(r['ns']) / int(r['operations']) for r in batch
                      if pass_index is None or int(r['pass_index']) == pass_index)

    summary = []
    for (c, n, a, w, v, phase), batch in sorted(groups.items()):
        assert len(batch) == 2 * receipt['reps']
        control = groups[c, n, a, 1, 0, phase]
        assert inventory(batch) == inventory(control), (c, n, a, w, v, phase)
        if a == 3:
            assert inventory(batch) == inventory(groups[c, n, 0, w, v, phase])
        row = dict(case=c, n=n, arm=a, N=w, consumer=v, phase=phase,
                   ns_per_operation=cost(batch),
                   over_full=cost(batch) / cost(groups[c, n, a, w, 0, phase]),
                   over_scalar=cost(batch) / cost(groups[c, n, a, 1, v, phase]),
                   over_abseil=cost(batch) / cost(groups[c, n, 1, w, v, phase]),
                   over_exact=cost(batch) / cost(groups[c, n, 2, w, v, phase]),
                   over_direct=cost(batch) / cost(groups[c, n, 3, w, v, phase]),
                   logical_bytes=int(batch[0]['requested']) + int(batch[0]['metadata']))
        for p in (0, 1):
            row[f'pass_{p}_ns'] = cost(batch, p)
            row[f'over_full_pass_{p}'] = cost(batch, p) / cost(groups[c, n, a, w, 0, phase], p)
            row[f'over_direct_pass_{p}'] = cost(batch, p) / cost(groups[c, n, 3, w, v, phase], p)
        summary.append(row)
    csv_write(out / 'summary.csv', summary)
    # Every selected-field contender and phase is retained, including unchanged
    # writes/misses as noise controls. No subtraction estimates a lookup cost.
    csv_write(out / 'paired.csv', [r for r in summary if r['consumer'] == 1])
    print(f'Validated {len(rows)} rows / {len(processes)} processes; consumers, packet widths and direct fields preserve inventories')
    for r in summary:
        if r['arm'] == 0 and r['consumer'] == 1 and r['phase'] in ('hit', 'aged_hit'):
            print(*(r[k] for k in ('case', 'n', 'N', 'phase')),
                  *(f'{r[k]:.3f}' for k in ('ns_per_operation', 'over_full', 'over_abseil', 'over_direct')))


def run(a):
    out = a.output
    out.mkdir(parents=True, exist_ok=True)
    assert not (out / 'processes.json').exists()
    receipt = dict(reps=a.reps, cases=a.cases, counts=a.counts, widths=a.widths,
                   binary_sha256={}, processes=[])
    files = {}
    for case in a.cases:
        name = f'hashtable_design_hierarchy_packet_{case}_consumer'
        target = out / name
        shutil.copy2(a.binaries / name, target)
        files[case] = target.resolve()
        receipt['binary_sha256'][name] = sha(target)
    order = [(c, n, arm, w, v) for c in a.cases for n in a.counts
             for arm in range(4) for w in a.widths for v in (0, 1)]
    random.Random(9771).shuffle(order)
    rows = []
    for p, sequence in enumerate((order, list(reversed(order)))):
        for c, n, arm, w, v in sequence:
            command = ['taskset', '-c', a.cpu, str(files[c]), str(w), str(arm), str(n), str(a.reps), str(v)]
            start = time.time()
            result = subprocess.run(command, capture_output=True, text=True)
            receipt['processes'].append(dict(case=c, n=n, arm=arm, width=w, consumer=v, pass_index=p,
                                             command=command, start=start, end=time.time(),
                                             returncode=result.returncode, stderr=result.stderr))
            (out / 'processes.json').write_text(json.dumps(receipt, indent=2) + '\n')
            assert result.returncode == 0 and not result.stderr, (command, result.stdout, result.stderr)
            batch = list(csv.DictReader(io.StringIO(result.stdout)))
            assert len(batch) == a.reps * len(PHASES)
            for row in batch:
                row.update(case=c, pass_index=p)
            rows.extend(batch)
    csv_write(out / 'values.csv', rows)
    report(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    r = sub.add_parser('run')
    r.add_argument('binaries', type=Path)
    r.add_argument('output', type=Path)
    r.add_argument('--cpu', required=True)
    r.add_argument('--cases', nargs='+', default=['16_16_1', '40_16_1', '40_16_2', '40_64_1', '40_64_2', '64_64_2'])
    r.add_argument('--counts', type=int, nargs='+', default=[64, 192, 768])
    r.add_argument('--widths', type=int, nargs='+', default=[1, 4, 8])
    r.add_argument('--reps', type=int, default=7)
    v = sub.add_parser('report')
    v.add_argument('output', type=Path)
    args = parser.parse_args()
    run(args) if args.mode == 'run' else report(args.output)
