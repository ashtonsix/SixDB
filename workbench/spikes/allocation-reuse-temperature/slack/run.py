#!/usr/bin/env python3
"""Compare current Window reuse, modest-slack preference and prepared best fit."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

from variants import write_variants

ROOT = Path(__file__).resolve().parents[4]
STUDY = Path('workbench/spikes/allocation-reuse-temperature/slack')


def run(command, log):
    with log.open('w') as output:
        subprocess.run([str(x) for x in command], check=True, stdout=output, stderr=subprocess.STDOUT)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    p.add_argument('--trials', type=int, default=6)
    p.add_argument('--operations', type=int, default=32768)
    p.add_argument('--rounds', type=int, default=4)
    p.add_argument('--slots', type=int, default=16)
    p.add_argument('--march', default='native')
    p.add_argument('--tune', default='generic')
    p.add_argument('--sanitizers', action='store_true')
    p.add_argument('--checks-only', action='store_true')
    args = p.parse_args()
    if min(args.trials, args.operations, args.rounds, args.slots) < 1:
        p.error('trial/operation/round/slot counts must be positive')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'source'
    run([sys.executable, ROOT / 'workbench/tools/capture.py', 'create', source], out / 'capture.log')
    variants = out / 'variants'
    write_variants(source / 'orbital/src/allocation/window.cpp', variants)
    build = out / 'build'
    configure = ['cmake', '-S', source / STUDY, '-B', build, '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release', '-DWINDOW_VARIANTS=' + str(variants),
                 '-DSIXDB_MARCH=' + args.march, '-DSIXDB_TUNE=' + args.tune]
    if args.sanitizers:
        configure += ['-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer',
                      '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
    run(configure, out / 'configure.log')
    run(['cmake', '--build', build, '-j', os.environ.get('SIXDB_BUILD_JOBS', '2')], out / 'build.log')
    policies = ['baseline', 'prefer-2x', 'best-fit']
    binaries = {name: build / ('reuse_' + name) for name in policies}
    for name, binary in binaries.items():
        run([binary, '--check', '--cpu', args.cpu], out / (name + '-check.log'))
        run(['taskset', '-c', args.cpu, build / ('check_' + name)], out / (name + '-window-check.log'))
    if args.checks_only:
        (out / 'qualification.json').write_text(json.dumps(dict(
            settings=vars(args), source_digest=json.loads(source.with_name('source.capture.json').read_text())['source_digest'],
            variant_sha256={name: digest(variants / (name + '.cpp')) for name in policies},
            binary_sha256={name: digest(binary) for name, binary in binaries.items()},
            status='all harness and existing Window consequence checks passed'
        ), indent=2, default=str) + '\n')
        print('Qualified', ', '.join(policies), flush=True)
        return
    rows, executions = [], []
    for trial in range(args.trials):
        rotation = trial % len(policies)
        order = policies[rotation:] + policies[:rotation]
        if (trial // len(policies)) % 2:
            order = list(reversed(order))
        for policy in order:
            for touch in ['64', 'full']:
                binary = binaries[policy]
                path = out / (policy + '-' + str(trial) + '-' + touch + '.csv')
                command = [str(binary), '--cpu', str(args.cpu), '--operations', str(args.operations),
                           '--rounds', str(args.rounds), '--slots', str(args.slots), '--touch-bytes', touch]
                with path.open('w') as stream, path.with_suffix('.log').open('w') as errors:
                    subprocess.run(command, check=True, stdout=stream, stderr=errors)
                for row in csv.DictReader(path.open()):
                    rows.append(dict(policy=policy, trial=trial, **row))
                executions.append(dict(policy=policy, trial=trial, touch=touch, command=command, csv=path.name))
                print('measured', policy, trial, touch, flush=True)
    with (out / 'samples.csv').open('w') as stream:
        writer = csv.DictWriter(stream, list(rows[0])); writer.writeheader(); writer.writerows(rows)
    receipt = json.loads(source.with_name('source.capture.json').read_text())
    context = dict(command=sys.argv, settings=vars(args), host=platform.uname()._asdict(),
                   cpu=args.cpu, source_digest=receipt['source_digest'],
                   variant_sha256={name: digest(variants / (name + '.cpp')) for name in policies},
                   binary_sha256={name: digest(binary) for name, binary in binaries.items()},
                   executions=executions,
                   scope='Actual current allocator, block means; no individual operation latency quantiles or RSS claim.')
    (out / 'context.json').write_text(json.dumps(context, indent=2, default=str) + '\n')


if __name__ == '__main__':
    main()
