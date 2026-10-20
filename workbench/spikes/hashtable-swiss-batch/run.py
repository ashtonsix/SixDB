#!/usr/bin/env python3
"""Sequential native callers, rotated/reversed across shared input seeds."""
import argparse
import csv
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--cpu', required=True)
p.add_argument('--binary-dir', type=Path)
p.add_argument('--cases', default='30_10_0,30_10_8,30_10_64,30_20_0,30_20_8')
p.add_argument('--quick', action='store_true')
p.add_argument('--arms', default='group16,batch64,tail64,boost,abseil')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
binary = a.binary_dir or a.build / 'workbench/spikes/hashtable-swiss-batch'
arms = a.arms.split(',')
(a.output.parent / 'treatments.json').write_text(json.dumps(arms) + '\n')
(a.output.parent / 'cases.json').write_text(json.dumps(a.cases.split(',')) + '\n')
for case in a.cases.split(','):
    cold = '_20_' in case
    settings = [('load', 85)]
    settings += [('spill', 890 * (1024 if cold else 1))]
    if a.quick:
        settings = [('spill', 890)]
    for setting, level in settings:
        for trial, seed in enumerate([713] if a.quick else [713, 991, 1729]):
            order = arms[trial:] + arms[:trial]
            if trial % 2:
                order.reverse()
            for arm in order:
                target = 'peers' if arm in ('boost', 'abseil') else arm
                command = ['taskset', '-c', a.cpu, str(binary / f'swiss_{target}_{case}'),
                           '--impl', arm, '--seed', str(seed),
                           '--load', str(level if setting == 'load' else 85),
                           '--requests', str(1 << (16 if a.quick else 20)),
                           '--repetitions', str(1 if a.quick else 3),
                           '--conditioning', str(256 << 20 if cold else 0),
                           '--churn', str(3 if a.quick else 10)]
                if setting == 'spill':
                    command += ['--target-spill', str(level)]
                name = f'{case}-{setting}{level}-seed{seed}-{arm}'
                print(name, flush=True)
                with (a.output / (name + '.csv')).open('w') as out, (a.output / (name + '.stderr')).open('w') as err:
                    subprocess.run(command, stdout=out, stderr=err, check=True, timeout=600)
                rows = list(csv.DictReader((a.output / (name + '.csv')).open()))
                if not rows or any(float(r['ns']) <= 0 for r in rows):
                    raise RuntimeError(f'invalid measurements {name}')
                (a.output / 'progress.json').write_text(json.dumps({'last_complete': name, 'command': command}) + '\n')
(a.output / 'complete.txt').write_text('complete\n')
