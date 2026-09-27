#!/usr/bin/env python3
"""Bounded, single-host queue calibration; no workload network sockets."""
from __future__ import annotations
import argparse
import gzip
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run


def cases(quick=False):
    duration = 200_000_000 if not quick else 20_000_000
    traffic = {'sparse': (1_000_000, 1), 'steady': (10_000, 1),
               'burst': (32_000, 32), 'pressure': (250, 1)}
    result = []
    def add(name, policy='spin', batch=1, payload=64, shape='burst', capacity=256,
            background='none', pause_ns=0, reverse=False, timing='full'):
        interval, burst = traffic[shape]
        result.append(dict(name=name, policy=policy, batch=batch, bytes=payload, interval=interval,
                           burst=burst, count=duration // interval * burst, capacity=capacity,
                           background=background, pause_ns=pause_ns, reverse=reverse, timing=timing))
    for policy in ['spin', 'wait', 'work']:
        for shape in traffic:
            for batch in [1, 32]:
                add(f'{policy}-{shape}-b{batch}', policy=policy, shape=shape, batch=batch)
    for payload in [1024, 16384]:
        for shape in ['steady', 'burst']:
            for batch in [1, 32]:
                add(f'payload{payload}-{shape}-b{batch}', payload=payload, shape=shape, batch=batch)
    add('spin-burst-b8', batch=8)
    for policy in ['spin', 'wait']:
        for capacity in [16, 256]:
            add(f'pause-{policy}-q{capacity}', policy=policy, capacity=capacity,
                batch=min(32, capacity), pause_ns=5_000_000)
    for policy in ['spin', 'work']:
        for shape in ['steady', 'burst']:
            add(f'neighbour-{policy}-{shape}', policy=policy, shape=shape, batch=32, background='stream')
    for policy in ['spin', 'wait', 'work']:
        add(f'reverse-{policy}-burst', policy=policy, batch=32, reverse=True)
    for batch in [1, 32]:
        add(f'reduced-pressure-b{batch}', shape='pressure', batch=batch, timing='reduced')
    add('reduced-burst-b32', batch=32, timing='reduced')
    return result


def confirmation_cases(quick=False):
    duration = 20_000_000 if quick else 100_000_000
    result = []
    def add(name, *, interval=32000, burst=32, batch=32, capacity=256,
            policy='spin', timing='full', pause_ns=0, payload=64):
        result.append(dict(name=name, policy=policy, batch=batch, bytes=payload,
            interval=interval, burst=burst, count=duration // interval * burst,
            capacity=capacity, background='none', pause_ns=pause_ns, reverse=False, timing=timing))
    for interval in [125, 64]:
        for batch in [1, 32]:
            for timing in ['full', 'reduced']:
                add(f'cliff-i{interval}-b{batch}-{timing}', interval=interval, burst=1, batch=batch, timing=timing)
    for policy in ['spin', 'wait']:
        for capacity in [16, 256]:
            for pause in [0, 5_000_000]:
                add(f'queue-{policy}-q{capacity}-pause{pause}', policy=policy,
                    capacity=capacity, batch=min(32, capacity), pause_ns=pause)
    for batch in [1, 32]:
        add(f'large-burst-b{batch}', payload=16384, batch=batch)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--panel', choices=['main', 'confirmation'], default='main')
    p.add_argument('--reps', type=int, default=3)
    p.add_argument('--build-dir', type=Path, default=ROOT / 'build/workspaces/orbital-local-handoff')
    args = p.parse_args()
    if not 1 <= args.reps <= 5:
        p.error('reps must be 1..5')
    allowed = sorted(os.sched_getaffinity(0))
    if len(allowed) < 3:
        p.error('requires at least three allowed CPUs')
    spec = importlib.util.spec_from_file_location('hardware', ROOT / 'workbench/spikes/memory-characterisation/hardware.py')
    hardware = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hardware)
    hw = hardware.discover(allowed[0])
    peer = next((x['cpu'] for x in hw['peers'] if x['relation'] == 'separate-core'), None)
    if peer is None:
        p.error('requires a reported separate-core peer')
    neighbour = next(x for x in allowed if x not in [allowed[0], peer])
    panel = (cases if args.panel == 'main' else confirmation_cases)(args.quick)
    output = args.output.resolve()
    run = Run(ROOT, output, dict(quick=args.quick, reps=args.reps, panel_name=args.panel, panel=panel), workspace=args.build_dir.resolve())
    error = None
    try:
        (output / 'hardware.json').write_text(json.dumps(hw, indent=2) + '\n')
        (output / 'environment.json').write_text(json.dumps({k: v for k, v in os.environ.items()
            if k in ['SIXDB_JOB', 'SIXDB_WORKER_ID', 'SIXDB_WORKER_REUSED', 'SIXDB_SOURCE_COMMIT']}, indent=2) + '\n')
        run.step('compiler', ['clang++-21', '--version'])
        run.step('lscpu', ['lscpu', '-e'])
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=orbital-local-handoff', '-DSIXDB_TUNE=generic', '-DSIXDB_MARCH='])
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', 'orbital_local_handoff', '-j', '2'])
        binary = run.build_dir / 'workbench/spikes/orbital-local-handoff/orbital_local_handoff'
        shutil.copy2(binary, output / 'probe')
        for name in ['compile_commands.json', 'CMakeCache.txt']:
            shutil.copy2(run.build_dir / name, output / name)
        run.step('correctness', [sys.executable, str(run.source_root / HERE.relative_to(ROOT) / 'check.py'), str(output / 'probe')], 'correctness.txt')
        executions = []
        for case in panel:
            for rep in range(args.reps):
                name = f"{case['name']}-r{rep}"
                producer, consumer = (peer, allowed[0]) if case['reverse'] else (allowed[0], peer)
                command = [str(output / 'probe'), '--producer', str(producer), '--consumer', str(consumer),
                           '--neighbour', str(neighbour), '--output', str(output / (name + '.csv'))]
                for k, v in case.items():
                    if k not in ['name', 'reverse']:
                        command += ['--' + k.replace('_', '-'), str(v)]
                run.step(name, command, name + '.json')
                with (output / (name + '.csv')).open('rb') as src, gzip.open(output / (name + '.csv.gz'), 'wb', compresslevel=1) as dst:
                    shutil.copyfileobj(src, dst)
                (output / (name + '.csv')).unlink()
                executions.append(dict(case=case, rep=rep, name=name, producer=producer, consumer=consumer, neighbour=neighbour))
                (output / 'executions.json').write_text(json.dumps(executions, indent=2) + '\n')
        run.step('analyse', [sys.executable, str(run.source_root / HERE.relative_to(ROOT) / 'analyze.py'), str(output)])
        run.compact(['hardware.json', 'environment.json', 'executions.json', 'summary.csv', 'correctness.txt'], [])
    except Exception as exc:
        error = exc
    finally:
        error = run.finish(error)
    if error:
        raise error
    print(output)


if __name__ == '__main__':
    main()
