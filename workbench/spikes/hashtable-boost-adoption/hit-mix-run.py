#!/usr/bin/env python3
"""Fixed query-multiset ordering and consumer controls for the hot 50/50 cliff."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--binaries', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpu', required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
records = []
for trial in range(2):
    for seed_index, seed in enumerate([713, 991, 1729]):
        for w, v, b in [(32, 0, 11), (32, 8, 11), (40, 0, 12), (40, 8, 11)]:
            arms = ['ikea', 'boost', 'abseil']
            rotation = (trial + seed_index) % 3
            arms = arms[rotation:] + arms[:rotation]
            if (trial + seed_index) % 2: arms.reverse()
            controls = [(50, order) for order in range(4)] + [(0, 0), (100, 0)]
            if trial: controls.reverse()
            for miss, order in controls:
                for sink in [0, 1]:
                    for impl in arms:
                        binary = a.binaries / (f'memory_ikea_{w}_{b}_{v}' if impl == 'ikea'
                                               else f'memory_peers_{w}_{v}')
                        stem = f'w{w}-v{v}-s{seed}-t{trial}-{impl}-m{miss}-o{order}-sink{sink}'
                        command = ['taskset', '-c', a.cpu, str(binary), '--impl', impl,
                            '--n', '13926', '--reserve', str(27852 if impl == 'abseil' else 13926),
                            '--seed', str(seed), '--requests', '1000000', '--repetitions', '3',
                            '--writes', '0', '--read-prefetch', '0', '--miss-only', str(miss),
                            '--query-order', str(order), '--hits-only', str(sink), '--pmu', '1']
                        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
                        started = time.time()
                        with (a.output/(stem+'.csv')).open('w') as out, (a.output/(stem+'.jsonl')).open('w') as err:
                            child = subprocess.run(command, stdout=out, stderr=err)
                        record = dict(w=w, v=v, b=b if impl == 'ikea' else 0, impl=impl,
                            seed=seed, trial=trial, miss=miss, order=order, sink=sink,
                            binary_sha256=digest, command=command, start=started, end=time.time(),
                            exit=child.returncode, file=stem+'.csv', counters=stem+'.jsonl')
                        records.append(record)
                        (a.output/'runs.json').write_text(json.dumps(records, indent=2)+'\n')
                        print(json.dumps(record), flush=True)
                        assert child.returncode == 0, record
                        rows = list(csv.DictReader((a.output/record['file']).open()))
                        reads = [r for r in rows if r['phase'] == 'read']
                        assert len(reads) == 3 and {int(r['miss']) for r in reads} == {miss}
                        assert {int(r['query_order']) for r in reads} == {order}
                        assert {int(r['hits_only']) for r in reads} == {sink}
                        counters = [json.loads(line) for line in (a.output/record['counters']).read_text().splitlines()]
                        assert len(counters) == 3
assert len(records) == 864
print('864 fixed-multiset hot-read processes complete', flush=True)
