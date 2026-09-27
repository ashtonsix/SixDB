#!/usr/bin/env python3
"""Finite queue/receipt checks; these do not validate timing accuracy."""
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from analyze import reduce_trial


def main(binary):
    cpus = sorted(os.sched_getaffinity(0))
    assert len(cpus) >= 3
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for policy in ['spin', 'wait', 'work']:
            for pressure in [False, True]:
                name = policy + ('-full' if pressure else '-drain')
                c = dict(name=name, policy=policy, count=64 if pressure else 1000,
                         interval=1 if pressure else 10000, burst=64 if pressure else 1,
                         capacity=2 if pressure else 256, batch=2 if pressure else 32,
                         bytes=1024, pause_ns=5_000_000 if pressure else 0, background='none')
                command = [str(Path(binary).resolve()), '--producer', str(cpus[0]), '--consumer', str(cpus[1]),
                           '--neighbour', str(cpus[2]), '--output', str(root / (name + '.csv'))]
                for k, v in c.items():
                    if k != 'name': command += ['--' + k.replace('_', '-'), str(v)]
                receipt = subprocess.run(command, check=True, capture_output=True, text=True, timeout=5).stdout
                (root / (name + '.json')).write_text(receipt)
                with gzip.open(root / (name + '.csv.gz'), 'wb') as out:
                    out.write((root / (name + '.csv')).read_bytes())
                execution = dict(name=name, case=c, rep=0)
                r = reduce_trial(root, execution)
                assert r['unfinished'] == 0
                if pressure:
                    assert (r['completed'], r['refused'], r['max_observed_depth']) == (2, 62, 2)
                original = json.loads(receipt)
                original['completed'] += 1
                (root / (name + '.json')).write_text(json.dumps(original))
                try:
                    reduce_trial(root, execution)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('forged completion count accepted')
                (root / (name + '.json')).write_text(receipt)
                raw = (root / (name + '.csv')).read_text().splitlines()
                raw[1] = '9,' + raw[1].split(',', 1)[1]
                with gzip.open(root / (name + '.csv.gz'), 'wt') as out:
                    out.write('\n'.join(raw) + '\n')
                try:
                    reduce_trial(root, execution)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('wrong offered identity accepted')
    print('6 native drain/full-queue checks and 12 observer negative controls passed')


if __name__ == '__main__':
    main(sys.argv[1])
