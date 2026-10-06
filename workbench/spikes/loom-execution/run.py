#!/usr/bin/env python3
"""Capture a bounded native Loom panel and the actual placement context."""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import random
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run


def topology():
    spec = importlib.util.spec_from_file_location('memory_hardware',
        ROOT / 'workbench/spikes/memory-characterisation/hardware.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    allowed = sorted(os.sched_getaffinity(0))
    facts = [module.discover(cpu) for cpu in allowed]
    physical = []
    seen = set()
    for hw in facts:
        siblings = tuple(hw['thread_siblings'])
        if siblings not in seen:
            seen.add(siblings)
            physical.append(hw['cpu'])
    first = facts[0]
    llc_level = max((int(c['level']) for c in first['caches']), default=0)
    same = next((p['cpu'] for p in first['peers'] if p['relation'] == 'separate-core'
                 and str(llc_level) in p['shared_cache_levels']), None)
    cross = next((p['cpu'] for p in first['peers'] if p['relation'] == 'separate-core'
                  and str(llc_level) not in p['shared_cache_levels']), None)
    placements = []
    if same is not None:
        placements.append(('same-llc', [physical[0], same]))
    if cross is not None:
        placements.append(('cross-llc', [physical[0], cross]))
    if not placements and len(physical) > 1:
        placements.append(('reported-separate-core', physical[:2]))
    return {'cpus': facts, 'physical_cpus': physical, 'pair_placements': placements}


def panel(hw, quick):
    cases = []
    physical = hw['physical_cpus']
    def add(name, target, arguments):
        cases.append({'name': name, 'target': target, 'args': list(map(str, arguments))})
    duration = 20 if quick else 100
    cpuarg = str(physical[0])
    abi_targets = ['loom_abi', 'loom_abi_ordinary']
    if hw['cpus'][0]['identity']['isa'] == 'x86_64':
        abi_targets.append('loom_abi_regcall')
    for target in abi_targets:
        for vectors in [0, 2, 8, 12]:
            for variant in ['direct', 'manual', 'ring']:
                add(f'abi-{target}-{variant}-v{vectors}', target,
                    ['--cpus', cpuarg, '--variant', variant, '--ring', 16, '--jobs', 16,
                     '--steps', 4096 if quick else 262144, '--vectors', vectors, '--heterogeneous'])
            if not quick:
                add(f'abi-underfill-{target}-v{vectors}', target,
                    ['--cpus', cpuarg, '--variant', 'ring', '--ring', 32, '--jobs', 1,
                     '--steps', 262144, '--vectors', vectors])
    for workload in ['chains', 'bec256']:
        dataset = (2048 if quick else 4194304) if workload == 'chains' else (2048 if quick else 1048576)
        for variant in ['direct', 'shared', 'manual', 'ring']:
            widths = [16] if variant in ['direct', 'shared'] or quick else [1, 8, 16, 32, 64, 128]
            for width in widths:
                for prefetch in ['none', 'first']:
                    add(f'micro-{workload}-{variant}-r{width}-{prefetch}', 'loom_microtask',
                        ['--workload', workload, '--variant', variant, '--ring', width,
                         '--overflow', 128, '--jobs', 128, '--steps', 256,
                         '--dataset-items', dataset, '--prefetch', prefetch, '--cpus', cpuarg])
    if not quick:
        for variant in ['direct', 'shared', 'manual', 'ring']:
            for width in ([16] if variant in ['direct', 'shared'] else [16, 32, 64]):
                add(f'micro-bec256-all-hints-{variant}-r{width}', 'loom_microtask',
                    ['--workload', 'bec256', '--variant', variant, '--ring', width,
                     '--overflow', 128, '--jobs', 128, '--steps', 256,
                     '--dataset-items', 1048576, '--prefetch', 'first', '--all-body-hints', '--cpus', cpuarg])
    for vectors in [False, True]:
        for variant in ['direct', 'shared', 'manual', 'ring']:
            args = ['--workload', 'chains', '--variant', variant, '--ring', 16,
                '--overflow', 0, '--jobs', 16, '--steps', 4096 if quick else 262144,
                '--dataset-items', 128, '--heterogeneous', '--cpus', cpuarg]
            if vectors:
                args += ['--vectors']
            add(f'warm-{variant}-vectors{int(vectors)}', 'loom_microtask', args)
            add(f'warm-ordinary-{variant}-vectors{int(vectors)}', 'loom_microtask_ordinary', args)
    if not quick:
        for jobs in [1, 4, 16, 128]:
            add(f'underfill-j{jobs}', 'loom_microtask',
                ['--workload', 'chains', '--variant', 'ring', '--ring', 32,
                 '--overflow', 128, '--jobs', jobs, '--steps', 4096,
                 '--dataset-items', 4194304, '--prefetch', 'first', '--cpus', cpuarg])
        for grain in [4, 16]:
            for variant in ['direct', 'manual', 'ring']:
                add(f'grain-{variant}-g{grain}', 'loom_microtask',
                    ['--workload', 'chains', '--variant', variant, '--ring', 32,
                     '--overflow', 128, '--jobs', 128, '--steps', 256,
                     '--dataset-items', 4194304, '--prefetch', 'first', '--grain', grain, '--cpus', cpuarg])
    for length in ([65536] if quick else [64, 4096, 65536]):
        for variant in ['direct', 'manual', 'ring']:
            for prefetch in ['none', 'first', 'pages']:
                for grain in ([1] if quick or length == 64 else [1, length // 64]):
                    add(f'range-b{length}-{variant}-{prefetch}-g{grain}', 'loom_microtask',
                        ['--workload', 'ranges', '--variant', variant, '--ring', 16,
                         '--overflow', 128, '--jobs', 128, '--range-bytes', length,
                         '--dataset-items', 128, '--prefetch', prefetch,
                         '--grain', grain, '--cpus', cpuarg])
    if not quick:
        # One extra line changes inter-range cache-set alignment. Keep it as a
        # control; a stream-count cliff alone cannot identify a prefetch table.
        for length in [65536, 65600]:
            for variant in ['direct', 'manual', 'ring']:
                for width in ([16] if variant == 'direct' else [16, 32, 48, 64]):
                    for prefetch in ['none', 'first']:
                        add(f'stream-b{length}-{variant}-r{width}-{prefetch}', 'loom_microtask',
                            ['--workload', 'ranges', '--variant', variant, '--ring', width,
                             '--overflow', 128, '--jobs', 4096, '--range-bytes', length,
                             '--prefetch', prefetch, '--grain', 1, '--cpus', cpuarg])
    variants = ['dense', 'sparse', 'inbox', 'group-direct', 'group-routed']
    for placement, cpus in hw['pair_placements']:
        cpuarg = ','.join(map(str, cpus))
        for variant in variants:
            for sample in [0, 64, 1]:
                args = ['--variant', variant, '--workers', 2, '--group-size', 2,
                    '--pattern', 'pingpong', '--capacity', 64, '--duration-ms', duration,
                    '--drain-ms', 500, '--cpus', cpuarg]
                if sample:
                    args += ['--instrumented', '--sample-every', sample]
                add(f'message-{placement}-{variant}-sample{sample}', 'loom_messaging', args)
        for variant in ['ttas', 'backoff', 'mcs', 'useful']:
            for pause in [0, 1000]:
                add(f'lock-{placement}-{variant}-pause{pause}', 'loom_affinity',
                    ['--family', 'locks', '--variant', variant, '--duration-ms', duration,
                     '--holder-pause-us', pause, '--cpus', cpuarg])
        for visits in ([128] if quick else [16, 128, 1024, 8192]):
            for time_budget in [0, 5000]:
                add(f'service-{placement}-v{visits}-t{time_budget}', 'loom_service',
                    ['--cpus', cpuarg, '--visits', visits, '--budget-ns', time_budget,
                     '--jobs', 32, '--steps', 4096 if quick else 32768,
                     '--dataset-items', 2048 if quick else 4194304,
                     '--messages', 100 if quick else 1000, '--interval-ns', 100000])
        for distribution in ['uniform', 'skew']:
            for variant in ['owner', 'owner-direct', 'shared', 'copy', 'replicate']:
                add(f'join-{placement}-{distribution}-{variant}', 'loom_affinity',
                    ['--family', 'join', '--variant', variant, '--cpus', cpuarg,
                     '--build-rows', 4096 if quick else 2097152,
                     '--probe-rows', 2048 if quick else 262144,
                     '--distribution', distribution, '--output-cap', 4096])
            if not quick:
                for route in ['inbox', 'group-routed']:
                    add(f'join-{placement}-{distribution}-owner-{route}', 'loom_affinity',
                        ['--family', 'join', '--variant', 'owner', '--cpus', cpuarg,
                         '--build-rows', 2097152, '--probe-rows', 262144,
                         '--distribution', distribution, '--output-cap', 4096, '--route-variant', route])
    sizes = [n for n in [8, 16, 32] if n <= len(physical)]
    if quick:
        sizes = sizes[:1]
    for n in sizes:
        cpuarg = ','.join(map(str, physical[:n]))
        for pattern in ['sparse', 'fanin', 'all-to-all', 'fanout']:
            for variant in variants:
                add(f'message-n{n}-{pattern}-{variant}', 'loom_messaging',
                    ['--variant', variant, '--workers', n, '--group-size', 4,
                     '--pattern', pattern, '--capacity', 64, '--duration-ms', duration,
                     '--drain-ms', 1000, '--cpus', cpuarg])
        if not quick:
            for pattern in ['fanin', 'all-to-all']:
                for variant in variants:
                    add(f'retry-n{n}-{pattern}-{variant}', 'loom_messaging',
                        ['--variant', variant, '--workers', n, '--group-size', 4,
                         '--pattern', pattern, '--capacity', 64, '--duration-ms', 1000,
                         '--messages', 10000, '--retry', '--drain-ms', 1000, '--cpus', cpuarg])
        for variant in ['dense', 'inbox', 'group-direct', 'group-routed']:
            for compact in [False, True]:
                args = ['--variant', variant, '--workers', n, '--group-size', 4,
                    '--pattern', 'fanin', '--capacity', 8, '--duration-ms', duration,
                    '--drain-ms', 1000, '--pause-worker', 0, '--pause-us', 5000,
                    '--pause-after', 1, '--cpus', cpuarg]
                if compact:
                    args += ['--compact']
                add(f'pressure-n{n}-{variant}-compact{int(compact)}', 'loom_messaging', args)
        if not quick:
            for group in [2, 8]:
                if group <= n:
                    add(f'group-n{n}-g{group}-all-to-all', 'loom_messaging',
                        ['--variant', 'group-direct', '--workers', n, '--group-size', group,
                         '--pattern', 'all-to-all', '--capacity', 64,
                         '--duration-ms', duration, '--drain-ms', 1000, '--cpus', cpuarg])
            for variant in ['ttas', 'backoff', 'mcs', 'useful']:
                add(f'lock-n{n}-{variant}', 'loom_affinity',
                    ['--family', 'locks', '--variant', variant, '--duration-ms', duration,
                     '--cpus', cpuarg])
                add(f'lock-writers{n-1}-{variant}', 'loom_affinity',
                    ['--family', 'locks', '--variant', variant, '--duration-ms', duration,
                     '--cpus', ','.join(map(str, physical[1:n]))])
            add(f'lock-owner-writers{n-1}', 'loom_affinity',
                ['--family', 'locks', '--variant', 'owner', '--duration-ms', duration,
                 '--cpus', cpuarg])
            for distribution in ['uniform', 'skew']:
                for variant in ['owner', 'owner-direct', 'shared', 'copy', 'replicate']:
                    add(f'join-n{n}-{distribution}-{variant}', 'loom_affinity',
                        ['--family', 'join', '--variant', variant, '--cpus', cpuarg,
                         '--build-rows', 524288, '--probe-rows', 262144,
                         '--distribution', distribution, '--output-cap', 256])
                for route in ['inbox', 'group-routed']:
                    add(f'join-n{n}-{distribution}-owner-{route}', 'loom_affinity',
                        ['--family', 'join', '--variant', 'owner', '--cpus', cpuarg,
                         '--build-rows', 524288, '--probe-rows', 262144,
                         '--distribution', distribution, '--output-cap', 256, '--route-variant', route])
            for variant in variants:
                add(f'empty-n{n}-{variant}', 'loom_messaging',
                    ['--variant', variant, '--workers', n, '--group-size', 4,
                     '--pattern', 'sparse', '--capacity', 64, '--duration-ms', duration,
                     '--drain-ms', 0, '--empty', '--cpus', cpuarg])
    return cases


def summarize(rows):
    result = []
    for name in dict.fromkeys(row['case'] for row in rows):
        values = [row for row in rows if row['case'] == name]
        result.append({'case': name, 'samples': len(values),
            'family': values[0]['family'], 'variant': values[0]['variant'],
            'unit': values[0]['unit'], 'parameters': values[0]['parameters'],
            'verified': all(v['verified'] for v in values),
            'elapsed_ns': [v['elapsed_ns'] for v in values],
            'items': [v['items'] for v in values],
            'ns_per_item': [v['elapsed_ns'] / v['items'] if v['items'] else None for v in values],
            'counters': {k: [v['counters'].get(k) for v in values]
                         for k in sorted(set().union(*(v['counters'] for v in values)))}})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--panel', choices=['initial', 'confirmation', 'extensions'], default='initial')
    p.add_argument('--case-regex', default='')
    p.add_argument('--reps', type=int, default=3)
    p.add_argument('--families', default='all')
    p.add_argument('--build-dir', type=Path, default=ROOT / 'build/workspaces/loom-execution-run')
    p.add_argument('--march', default=os.environ.get('LOOM_SPIKE_MARCH', ''))
    p.add_argument('--tune', default=os.environ.get('LOOM_SPIKE_TUNE', 'generic'))
    args = p.parse_args()
    if not 1 <= args.reps <= 10:
        p.error('reps must be 1..10')
    hw = topology()
    if args.panel == 'confirmation':
        from confirmation import panel as selected_panel
    elif args.panel == 'extensions':
        from extensions import panel as selected_panel
    else:
        selected_panel = panel
    cases = selected_panel(hw, args.quick)
    if args.case_regex:
        cases = [c for c in cases if re.search(args.case_regex, c['name'])]
    if args.families != 'all':
        cases = [c for c in cases if c['target'].removeprefix('loom_').split('_')[0] in args.families.split(',')]
    output = args.output.resolve()
    run = Run(ROOT, output, vars(args) | {'output': str(output),
        'build_dir': str(args.build_dir), 'cases': cases}, workspace=args.build_dir)
    error = None
    try:
        (output / 'hardware.json').write_text(json.dumps(hw, indent=2) + '\n')
        environment_names = ['SIXDB_JOB', 'SIXDB_WORKER_ID', 'SIXDB_WORKER_REUSED',
            'SIXDB_SOURCE_COMMIT', 'SIXDB_GROUP_MEMBER', 'SIXDB_BUILD_JOBS',
            'LOOM_SPIKE_MARCH', 'LOOM_SPIKE_TUNE']
        (output / 'environment.json').write_text(json.dumps({k: os.environ[k] for k in environment_names
            if k in os.environ}, indent=2) + '\n')
        run.step('compiler', ['clang++-21', '--version'])
        run.step('lscpu', ['lscpu', '-e'])
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=loom-execution',
            '-DSIXDB_MARCH=' + args.march, '-DSIXDB_TUNE=' + args.tune])
        targets = sorted({case['target'] for case in cases})
        if not targets:
            raise ValueError('selected panel has no runnable cases')
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', *targets,
                           '-j', os.environ.get('SIXDB_BUILD_JOBS', '2')])
        binary_root = run.build_dir / 'workbench/spikes/loom-execution'
        assemblies = []
        for target in targets:
            run.step('check-' + target, ['timeout', '60', str(binary_root / target), '--check',
                                      '--cpus', ','.join(map(str, hw['physical_cpus'][:4]))])
            if target.startswith('loom_abi') or (args.panel in ['confirmation', 'extensions'] and
                                                target in ['loom_microtask', 'loom_messaging', 'loom_service',
                                                           'loom_inventory', 'loom_pressure']):
                run.step('assembly-' + target, ['llvm-objdump-21', '-d', '--demangle',
                                              str(binary_root / target)])
                assemblies.append(output / ('assembly-' + target + '.stdout'))
        if assemblies:
            run.step('codegen', ['python3', str(run.source_root / 'workbench/spikes/loom-execution/codegen.py'),
                               *map(str, assemblies), '--output', str(output / 'codegen.json')])
        rows = []
        for repeat in range(args.reps):
            order = list(cases)
            random.Random(1801 + repeat).shuffle(order)
            for case in order:
                name = case['name'] + '-r' + str(repeat)
                filename = name + '.jsonl'
                run.step(name, ['timeout', '60', str(binary_root / case['target']),
                         *case['args'], '--repeat', str(repeat)], filename)
                values = [json.loads(line) for line in (output / filename).read_text().splitlines()]
                if not values or not all(v.get('verified') is True for v in values):
                    raise RuntimeError('unverified case: ' + name)
                if args.panel == 'confirmation' and case['target'] == 'loom_messaging':
                    for value in values:
                        counters = value['counters']
                        if not (counters['completed'] == counters['accepted'] == counters['planned_unique'] and
                                not any(counters[key] for key in ['unoffered', 'unique_unaccepted', 'unfinished'])):
                            raise RuntimeError('fixed-demand messaging did not complete equal work: ' + name)
                rows.extend(v | {'case': case['name']} for v in values)
                (output / 'rows.jsonl').write_text(''.join(json.dumps(v, sort_keys=True) + '\n' for v in rows))
        (output / 'summary.json').write_text(json.dumps(summarize(rows), indent=2) + '\n')
        run.compact(['run.json', 'hardware.json', 'environment.json', 'compiler.stdout',
                     'rows.jsonl', 'summary.json', *(['codegen.json'] if assemblies else [])], [])
    except Exception as exc:
        error = exc
        raise
    finally:
        run.finish(error)


if __name__ == '__main__':
    main()
