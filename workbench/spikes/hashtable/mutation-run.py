#!/usr/bin/env python3
"""Balanced fresh-process complete-operation mutation comparison."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import random
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('binaries', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--cpu', required=True)
p.add_argument('--reps', type=int, default=5)
p.add_argument('--widths', type=int, nargs='+', default=[2, 8, 16])
p.add_argument('--variants', nargs='+', default=['control', 'fused', 'partial'])
p.add_argument('--short', action='store_true', help='W2 diagnostic boundary subset')
a = p.parse_args()
assert a.reps > 0
fans = {2: [1, 2, 4, 5, 8, 9, 16, 32], 8: [1, 2, 3, 4, 8, 32], 16: [1, 2, 3, 8, 32]}
if a.short:
    assert a.widths == [2]
    fans[2] = [1, 4, 5, 8, 9]
variants = a.variants
assert len(variants) >= 2 and len(set(variants)) == len(variants)
assert all(v.isidentifier() for v in variants)
binaries = {(v, w): (a.binaries / v / f'values-{w}').resolve() for v in variants for w in a.widths}
a.output.mkdir(parents=True, exist_ok=True)
receipt = {'binary_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in binaries.values()},
           'schedule_seed': 947, 'processes': []}
schedule = [(w, k, arm, fanout, large) for w in a.widths for k in (1, 2) for arm in range(5)
            for fanout in ([1] if k == 1 else fans[w]) for large in (0, 1)]
random.Random(947).shuffle(schedule)
with (a.output / 'mutation.csv').open('w') as output:
    writer = None
    for case, (w, kind, arm, fanout, large) in enumerate(schedule):
        order = variants[case % len(variants):] + variants[:case % len(variants)]
        order = order + list(reversed(order))
        for position, variant in enumerate(order):
            cmd = ['taskset', '-c', a.cpu, str(binaries[variant, w]), str(kind), str(arm), str(fanout), str(large), str(a.reps)]
            start = time.time()
            result = subprocess.run(cmd, capture_output=True, text=True)
            receipt['processes'].append({'argv': cmd, 'case': case, 'order': position, 'variant': variant,
                                         'started_unix': start, 'ended_unix': time.time(),
                                         'returncode': result.returncode, 'stderr': result.stderr})
            (a.output / 'processes.json').write_text(json.dumps(receipt, indent=2))
            if result.returncode:
                raise RuntimeError((cmd, result.stderr, result.stdout))
            rows = list(csv.DictReader(io.StringIO(result.stdout)))
            assert len(rows) == a.reps * 9
            for row in rows:
                row.update(variant=variant, case=case, order=position)
            if writer is None:
                writer = csv.DictWriter(output, fieldnames=list(rows[0]))
                writer.writeheader()
            writer.writerows(rows)
            output.flush()
        print(f'{case+1}/{len(schedule)} W{w} kind{kind} arm{arm} fanout{fanout} large{large}', flush=True)
print('all complete-operation oracles passed')
