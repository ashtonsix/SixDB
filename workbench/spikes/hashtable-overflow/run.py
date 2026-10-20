#!/usr/bin/env python3
"""Sequential complete callers; rotate implementations across input seeds."""
import argparse
import csv
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--cpu', required=True)
p.add_argument('--quick', action='store_true')
p.add_argument('--cases', help='comma-separated K_b_V cases')
p.add_argument('--loads', help='primary occupancy percentages, or target spill levels with --density')
p.add_argument('--binary-dir', type=Path)
p.add_argument('--density', action='store_true')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
binary = a.binary_dir or a.build / 'workbench/spikes/hashtable-overflow'
cases = ['30_10_0', '30_10_8', '30_10_64', '14_10_0', '14_10_8', '30_20_0', '30_20_8']
if a.quick:
    cases = ['30_10_8']
if a.cases:
    cases = a.cases.split(',')
arms = ['baseline', 'tag8', 'filter15', 'boost', 'abseil']
if a.density:
    arms = ['baseline', 'tag8', 'tag8_c30', 'tag8_c31', 'filter15', 'filter15_c30', 'filter15_c31', 'boost', 'abseil']
for case in cases:
    cold = '_20_' in case
    loads = [85] if cold or a.quick else [50, 85, 95]
    if a.density:
        loads = [925, 980]
    if a.loads:
        loads = [int(x) for x in a.loads.split(',')]
    for load in loads:
        for trial, seed in enumerate([713, 991, 1729] if not a.quick else [713]):
            order = arms[trial:] + arms[:trial]
            if trial % 2:
                order.reverse()
            for arm in order:
                target = 'peers' if arm in ('boost', 'abseil') else arm
                command = ['taskset', '-c', a.cpu, str(binary / f'overflow_{target}_{case}'),
                           '--impl', arm, '--load', str(85 if a.density else load), '--seed', str(seed),
                           '--requests', str(1 << (16 if a.quick else 20)),
                           '--repetitions', str(1 if a.quick else 3),
                           '--conditioning', str(256 << 20 if cold else 0),
                           '--churn', str(3 if a.quick else 10)]
                name = f'{case}-load{load}-seed{seed}-{arm}'
                if a.density:
                    target_spill = load * (1024 if cold else 1)
                    command += ['--target-spill', str(target_spill)]
                    name = f'{case}-spill{target_spill}-seed{seed}-{arm}'
                print(name, flush=True)
                with (a.output / (name + '.csv')).open('w') as out, (a.output / (name + '.stderr')).open('w') as err:
                    subprocess.run(command, stdout=out, stderr=err, check=True, timeout=600)
                rows = list(csv.DictReader((a.output / (name + '.csv')).open()))
                if not rows or any(float(r['ns']) <= 0 for r in rows):
                    raise RuntimeError(f'invalid output: {name}')
                (a.output / 'progress.json').write_text(json.dumps({'last_complete': name, 'command': command}) + '\n')
(a.output / 'complete.txt').write_text('complete\n')
