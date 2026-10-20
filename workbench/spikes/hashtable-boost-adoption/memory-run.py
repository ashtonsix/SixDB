#!/usr/bin/env python3
"""Actual native capacity census and counterbalanced full-table measurements."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cpu', default=os.environ.get('SIXDB_CPU', '0'))
parser.add_argument('--requests', default=1_000_000, type=int)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
samples = args.output / 'samples'
samples.mkdir(exist_ok=True)
binaries = args.build / 'workbench/spikes/hashtable-boost-adoption'
cases = [
    dict(name='hot_w24', w=24, n=27852, bs=[10, 11, 12, 13]),
    dict(name='hot_w32', w=32, n=13926, bs=[10, 11, 12]),
    dict(name='hot_w40', w=40, n=13926, bs=[10, 11, 12]),
    dict(name='cold_w40', w=40, n=14260633, bs=[20, 21, 22]),
    dict(name='cold_w50', w=50, n=14260633, bs=[20, 21, 22])]
log = args.output / 'progress.jsonl'
plans = []

def progress(event, **fields):
    with log.open('a') as f:
        f.write(json.dumps(dict(event=event, time=time.time(), **fields)) + '\n')
    print(event, fields, flush=True)

def execute(binary, impl, options, output=None):
    # A resident executable is the same policy for every arm/trial.
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    cmd = ['taskset', '-c', args.cpu, str(binary), '--impl', impl]
    for key, value in options.items():
        cmd += ['--' + key, str(value)]
    if output:
        with output.open('w') as f, output.with_suffix('.stderr').open('w') as err:
            start = time.monotonic()
            child = subprocess.run(cmd, stdout=f, stderr=err)
        rows = list(csv.DictReader(output.open()))
        record = dict(command=cmd, binary_sha256=digest, exit=child.returncode,
                      elapsed_seconds=time.monotonic() - start, rows=len(rows))
        output.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')
        if child.returncode:
            raise RuntimeError(f'{output}: exit {child.returncode}; see stderr')
        return rows
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return list(csv.DictReader(io.StringIO(result.stdout)))

for case in cases:
    for v in [0, 8]:
        n, w = case['n'], case['w']
        peer = binaries / f'memory_peers_{w}_{v}'
        census = []
        for impl in ['boost', 'abseil']:
            for reservation in ([n] if impl == 'boost' else [n, n * 2]):
                row, = execute(peer, impl, dict(n=n, reserve=reservation, census=1))
                census.append(dict(impl=impl, b=0, reservation=reservation, row=row,
                                   binary=str(peer)))
        for b in case['bs']:
            binary = binaries / f'memory_ikea_{w}_{b}_{v}'
            row, = execute(binary, 'ikea', dict(n=n, census=1))
            census.append(dict(impl='ikea', b=b, reservation=0, row=row, binary=str(binary)))
        boost = next(c for c in census if c['impl'] == 'boost')
        budget = int(boost['row']['bytes'])
        abseil = [c for c in census if c['impl'] == 'abseil']
        ikeas = [c for c in census if c['impl'] == 'ikea']
        low = max((c for c in ikeas if int(c['row']['bytes']) <= budget),
                  key=lambda c: int(c['row']['bytes']))
        high = min((c for c in ikeas if int(c['row']['bytes']) > budget),
                   key=lambda c: int(c['row']['bytes']), default=None)
        chosen = [('boost_native', boost), ('abseil_native', abseil[0]),
                  ('ikea_native', ikeas[0])]
        if int(abseil[-1]['row']['bytes']) != int(abseil[0]['row']['bytes']):
            chosen.append(('abseil_budget', abseil[-1]))
        if low['b'] != ikeas[0]['b']:
            chosen.append(('ikea_budget_lower', low))
        if high and int(low['row']['bytes']) / budget < .90:
            chosen.append(('ikea_budget_upper', high))
        plan = dict(**case, v=v, budget=budget, census=census,
                    arms=[dict(name=name, **arm) for name, arm in chosen])
        plans.append(plan)
        progress('census', case=case['name'], v=v, budget=budget,
                 arms=[(a['name'], a['row']['bytes']) for a in plan['arms']])
(args.output / 'plan.json').write_text(json.dumps(plans, indent=2) + '\n')

# Alternate cases and arms rather than running all samples of one library first.
schedule = []
for seed_index, seed in enumerate([713, 991, 1729]):
    for trial in range(5):
        for p in plans:
            cold = p['name'].startswith('cold')
            if cold and trial >= 2:
                continue
            arms = p['arms']
            rotation = (seed_index * 5 + trial) % len(arms)
            order = arms[rotation:] + arms[:rotation]
            if (seed_index + trial) % 2:
                order = list(reversed(order))
            for position, arm in enumerate(order):
                name = f"{p['name']}-v{p['v']}-s{seed}-t{trial}-{arm['name']}"
                output = samples / (name + '.csv')
                opts = dict(n=p['n'], reserve=arm['reservation'], seed=seed,
                            requests=args.requests, repetitions=3, churn=8,
                            conditioning=256 * 1024 ** 2 if cold else 0)
                record = dict(case=p['name'], w=p['w'], v=p['v'], n=p['n'], seed=seed,
                              trial=trial, position=position, arm=arm['name'],
                              b=arm['b'], budget=p['budget'], file=str(output.name))
                schedule.append(record)
                (args.output / 'schedule.json').write_text(json.dumps(schedule, indent=2) + '\n')
                progress('start', **record)
                rows = execute(Path(arm['binary']), arm['impl'], opts, output)
                assert {int(r['n']) for r in rows} == {p['n']}
                assert {int(r['k']) + int(r['b']) for r in rows} == {p['w']}
                assert all(float(r['ns']) > 0 for r in rows)
                progress('end', **record, rows=len(rows))
progress('complete', processes=len(schedule))
