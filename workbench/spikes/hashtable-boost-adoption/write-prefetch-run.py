#!/usr/bin/env python3
"""Bounded paired cold-write prefetch follow-up on the same fixed populations."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--binaries', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpu', default=os.environ.get('SIXDB_CPU', '0'))
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
records = []
for trial in range(2):
    for seed_index, seed in enumerate([713, 991, 1729]):
        for w in [40, 50]:
            for v in [0, 8]:
                n = 14260633
                b = 22 if v == 0 else 21
                arms = [('ikea', 0, n), ('ikea', 32, n), ('abseil', 0, 2*n),
                        ('abseil', 32, 2*n), ('boost', 0, n)]
                rotate = (trial * 3 + seed_index) % len(arms)
                arms = arms[rotate:] + arms[:rotate]
                if (trial + seed_index) % 2: arms = list(reversed(arms))
                for position, (impl, distance, reservation) in enumerate(arms):
                    name = (f'memory_ikea_{w}_{b}_{v}' if impl == 'ikea'
                            else f'memory_peers_{w}_{v}')
                    binary = a.binaries / name
                    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
                    stem = a.output / f'w{w}-v{v}-s{seed}-t{trial}-{impl}-p{distance}'
                    cmd = ['taskset', '-c', a.cpu, str(binary), '--impl', impl,
                           '--n', str(n), '--reserve', str(reservation), '--seed', str(seed),
                           '--requests', '1000000', '--reads', '0', '--writes', '1',
                           '--write-prefetch', str(distance), '--repetitions', '1',
                           '--churn', '8', '--conditioning', str(256 * 1024**2)]
                    started = time.time()
                    with stem.with_suffix('.csv').open('w') as out, stem.with_suffix('.stderr').open('w') as err:
                        result = subprocess.run(cmd, stdout=out, stderr=err)
                    record = dict(w=w, v=v, n=n, b=b if impl == 'ikea' else 0,
                                  seed=seed, trial=trial, position=position, impl=impl,
                                  prefetch=distance, binary_sha256=digest, command=cmd,
                                  start=started, end=time.time(), exit=result.returncode,
                                  file=stem.with_suffix('.csv').name)
                    records.append(record)
                    (a.output/'runs.json').write_text(json.dumps(records, indent=2)+'\n')
                    print(json.dumps(record), flush=True)
                    if result.returncode: raise RuntimeError(record)
                    rows = list(csv.DictReader(stem.with_suffix('.csv').open()))
                    assert len([r for r in rows if r['phase'] == 'refill']) == 7
                    assert all(int(r['n']) == n and int(r['peak_bytes']) >= int(r['bytes']) for r in rows)
print('120 paired/control cold-write processes complete', flush=True)
