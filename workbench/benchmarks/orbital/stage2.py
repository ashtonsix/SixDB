#!/usr/bin/env python3
"""Print or run one carried-forward experiment through the existing worker."""
import argparse
from pathlib import Path
import shlex
import subprocess

RECIPES = ('scalar', 'prepared-same-cpu', 'prepared-extra-cpu', 'lookup-widths',
           'pmu-availability', 'memory-sensitivity', 'composed-counters',
           'lifecycle-1', 'lifecycle-16', 'lifecycle-64')


def command(args):
    script = 'workbench/benchmarks/orbital/run.sh'
    environment, workload = [], []
    deadline = 14400
    if args.recipe.startswith('lifecycle-'):
        script = 'workbench/benchmarks/orbital/pressure.sh'
        deadline = 1800
        environment = ['ORBITAL_PRESSURE_ROUNDS=64', 'ORBITAL_REPETITIONS=3']
        workload = ['--slots', args.recipe.removeprefix('lifecycle-')]
    elif args.recipe == 'pmu-availability':
        script = 'workbench/benchmarks/orbital/pmu.sh'
        deadline = 900
    elif args.recipe == 'memory-sensitivity':
        script = 'workbench/spikes/memory-characterisation/pmu.sh'
        deadline = 3600
        if args.machine == 'neoverse-v2':
            environment.append('MEMORY_ARM_CACHE_EVENTS=1')
    else:
        workload = ['--records', '8192,262144,4194304', '--operations', '100000',
                    '--repetitions', '3']
        if args.recipe.startswith('prepared-'):
            workload += ['--prepared-pages', '8', '--preparer-cpu',
                         '-1' if args.recipe == 'prepared-same-cpu' else '1']
        else:
            workload += ['--prepared-pages', '0']
        if args.recipe in ('lookup-widths', 'composed-counters'):
            environment += ['ORBITAL_READ_EXECUTIONS=native,manual,ring',
                            'ORBITAL_CACHE_SETUPS=loaded,warm-trace']
            workload += ['--batch-width', '64', '--backends', 'packed-owner,lmdb,lmdb-read-batch64',
                         '--workloads', 'point-hit,point-miss,dependent-16']
            if args.recipe == 'lookup-widths':
                environment.append('ORBITAL_READ_ACTIVES=1,16,32,64')
            else:
                workload += ['--modes', 'throughput', '--pmu', 'stalls', '--memory-receipt', 'on']
    if args.machine == 'neoverse-v2':
        environment += ['ORBITAL_MARCH=armv9-a', 'ORBITAL_TUNE=neoverse-v2']
    for event in args.pmu_event:
        workload += ['--pmu-event', event]
    result = ['python3', 'workbench/tools/worker.py', 'run', script, '--source', str(args.source),
              '--machine', args.machine, '--instance-type', args.instance_type or
              ('c8a.xlarge' if args.machine == 'zen5' else 'c8g.xlarge'),
              '--capacity', 'on-demand', '--idle-seconds', str(args.idle_seconds),
              '--deadline', str(deadline), '--detach']
    for entry in environment:
        result += ['--env', entry]
    if workload:
        result += ['--'] + workload
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog=
        'Default prints a command and launches nothing. Run on Linux (prefix with orb -m ubuntu on Mac). '
        'Supply the stage-2 source capture; retained stage-1 data is a baseline, not a replacement cut. '
        'Each invocation selects one experiment; no automatic follow-on queue.')
    parser.add_argument('recipe', choices=RECIPES)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--machine', choices=('zen5', 'neoverse-v2'), default='zen5')
    parser.add_argument('--instance-type', help='Explicit worker size override for event qualification')
    parser.add_argument('--pmu-event', action='append', default=[], metavar='NAME:TYPE:CONFIG',
                        help='Qualified event added to memory-sensitivity or composed-counters')
    parser.add_argument('--idle-seconds', type=int, default=0,
                        help='0 settles the worker after the job; a positive value allows compatible paired reuse')
    parser.add_argument('--run', action='store_true', help='Dispatch the printed experiment')
    args = parser.parse_args()
    if args.idle_seconds < 0:
        parser.error('--idle-seconds must be nonnegative')
    if args.pmu_event and args.recipe not in ('memory-sensitivity', 'composed-counters'):
        parser.error('--pmu-event applies to memory-sensitivity or composed-counters')
    receipt = args.source.with_name(args.source.name + '.capture.json')
    if not args.source.is_dir() or not receipt.is_file():
        parser.error('--source must name an existing source capture with its adjacent receipt')
    chosen = command(args)
    print(shlex.join(chosen), flush=True)
    if args.run:
        raise SystemExit(subprocess.run(chosen).returncode)


if __name__ == '__main__':
    main()
