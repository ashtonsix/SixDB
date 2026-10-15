#!/usr/bin/env python3
"""Actual-table timings of three selector-quality counterexamples and seed0 controls."""
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

p = argparse.ArgumentParser(__doc__)
p.add_argument('binaries', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--cpu', required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
assert not (a.output/'values.csv').exists(), 'use a new directory for a new timing run'
files = {}
for variant in ('fold', 'multiply'):
    name = f'hashtable_design_selector_witness_{variant}'
    source, target = a.binaries/name, a.output/name
    shutil.copy2(source, target)
    files[variant] = target.resolve()
receipt = dict(binary_sha256={v: hashlib.sha256(f.read_bytes()).hexdigest() for v, f in files.items()}, processes=[])
order = [(k, seed, arm, variant) for k in (24, 32, 40) for seed in (0, 57)
         for arm in range(3) for variant in files]
random.Random(811).shuffle(order)
rows = []
for pass_index, history in enumerate((order, list(reversed(order)))):
    for k, seed, arm, variant in history:
        command = ['taskset', '-c', a.cpu, str(files[variant]), str(k), str(arm), str(seed), '5']
        start = time.time()
        result = subprocess.run(command, text=True, capture_output=True)
        receipt['processes'].append(dict(command=command, start=start, end=time.time(),
            returncode=result.returncode, stderr=result.stderr))
        (a.output/'processes.json').write_text(json.dumps(receipt, indent=2)+'\n')
        assert result.returncode == 0 and not result.stderr, (command, result.stdout, result.stderr)
        batch = list(csv.DictReader(io.StringIO(result.stdout)))
        assert len(batch) == 35
        for row in batch:
            assert (int(row['K']), int(row['arm']), int(row['seed']), int(row['selector'])) == (k, arm, seed, int(variant == 'multiply'))
            row.update(variant=variant, pass_index=pass_index)
        rows.extend(batch)
with (a.output/'values.csv').open('w') as out:
    writer = csv.DictWriter(out, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
groups = defaultdict(list)
for row in rows:
    groups[tuple(row[k] for k in ('K', 'seed', 'arm', 'variant', 'phase'))].append(row)
cost = lambda group: median(float(r['ns'])/int(r['operations']) for r in group)
summaries = []
for key, batch in sorted(groups.items()):
    k, seed, arm, variant, phase = key
    assert len(batch) == 10
    control = groups[k, seed, arm, 'fold', phase]
    for field in ('operations', 'requested', 'metadata', 'primary', 'secondary', 'overflow'):
        assert sorted(int(r[field]) for r in batch) == sorted(int(r[field]) for r in control)
    summary = dict(zip(('K', 'seed', 'arm', 'variant', 'phase'), key))
    summary.update(ns_per_operation=cost(batch), over_fold=cost(batch)/cost(control),
        over_abseil=cost(batch)/cost(groups[k, seed, '1', variant, phase]),
        over_exact=cost(batch)/cost(groups[k, seed, '2', variant, phase]))
    for pass_index in (0, 1):
        summary[f'pass_{pass_index}_ns'] = cost([r for r in batch if r['pass_index'] == pass_index])
    summaries.append(summary)
with (a.output/'summary.csv').open('w') as out:
    writer = csv.DictWriter(out, fieldnames=list(summaries[0])); writer.writeheader(); writer.writerows(summaries)
print(f'Validated {len(rows)} rows from {len(receipt["processes"])} processes; paired inventories agree')
for row in summaries:
    if row['arm'] == '0' and row['variant'] == 'multiply' and row['phase'] in ('hit', 'miss', 'serial'):
        print(*(row[k] for k in ('K', 'seed', 'phase')),
              *(f'{row[k]:.3f}' for k in ('ns_per_operation', 'over_fold', 'over_abseil', 'over_exact')))
