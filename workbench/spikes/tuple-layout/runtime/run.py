#!/usr/bin/env python3
"""Capture once, build per ISA, check and measure runtime TuplePack recipes."""

import argparse
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'workbench/tools'))
from dev import ROOT, checkout_root
from evidence import digest
from experiment import Run

STUDY = Path('workbench/spikes/tuple-layout')
PROFILES = {'neon': 'armv8-a+simd', 'avx2': 'x86-64-v3', 'avx512': 'znver5'}
TARGETS = ['tuple_runtime_check', 'tuple_runtime_bench']


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def native_path(path):
    path = path.expanduser().resolve()
    existing = path
    while not existing.exists():
        existing = existing.parent
    return checkout_root(existing, Path.home()) / path.relative_to(existing)


def export_samples(raw, destination):
    """Preserve individual Google Benchmark rows and all study-owned counters."""
    data = json.loads(raw.read_bytes())
    if any(row.get('error_occurred') for row in data['benchmarks']):
        raise ValueError(f'Benchmark reported an error: {raw}')
    rows = [row for row in data['benchmarks'] if row.get('run_type', 'iteration') == 'iteration']
    if not rows:
        raise ValueError(f'No individual repetitions: {raw}')
    for row in rows:
        if (not row.get('name') or row.get('iterations', 0) <= 0 or
                row.get('time_unit') not in {'ns', 'us', 'ms', 's'} or
                any(not math.isfinite(row[key]) or row[key] < 0 for key in ('cpu_time', 'real_time'))):
            raise ValueError(f'Invalid measurement: {row}')
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with destination.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        for row in rows:
            writer.writerow({key: value if isinstance(value, str) else json.dumps(value)
                             for key, value in row.items()})
    return data.get('context', {}), len(rows)


def run_profile(run, profile, args):
    output = run.output / profile
    output.mkdir()
    build = run.build_dir / f'{profile}-{args.tune}'
    configure = ['cmake', '-S', run.source_root, '-B', build, '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=clang++-21',
                 '-DSIXDB_SPIKES=tuple-layout', '-DSIXDB_BENCHMARKS=',
                 '-DSIXDB_MARCH=' + PROFILES[profile], '-DSIXDB_TUNE=' + args.tune,
                 '-DCMAKE_CXX_FLAGS=']
    existing = (build / 'build.ninja').exists()
    run.step(profile + '/configure', configure)
    for name in ['CMakeCache.txt', 'compile_commands.json', 'build.ninja']:
        shutil.copy2(build / name, output / name)
    shutil.copy2(build / 'CMakeFiles/rules.ninja', output / 'rules.ninja')
    if (build / '.ninja_log').exists():
        shutil.copy2(build / '.ninja_log', output / 'build-before.ninja_log')
    run.step(profile + '/build', ['cmake', '--build', build, '--target', *TARGETS,
                                 '-j', str(args.jobs)])
    shutil.copy2(build / '.ninja_log', output / '.ninja_log')
    run.step(profile + '/commands', ['ninja', '-C', build, '-t', 'commands', *TARGETS])
    binaries = output / 'bin'
    binaries.mkdir()
    for name in [*TARGETS, 'libtuple_runtime.a']:
        shutil.copy2(build / STUDY / name, binaries / name)
    identities = {path.name: digest(path) for path in binaries.iterdir()}
    run.step(profile + '/size', ['llvm-size-21', *(binaries / name for name in identities)],
             profile + '/sizes.txt')
    checker, benchmark = (binaries / name for name in TARGETS)
    pinned = ['taskset', '-c', str(args.cpu)]
    run.step(profile + '/check', [*pinned, checker], profile + '/checks.txt')
    run.step(profile + "/extra-check", [*pinned, benchmark, "--check-extra"], profile + "/extra-checks.txt")
    description = output / 'description.json'
    run.step(profile + '/describe', [*pinned, benchmark, '--describe', description])
    # The benchmark owns the schema. Retain exact bytes; do not sort maps or
    # reinterpret recipe controls as a portable production description.
    json.loads(description.read_bytes())
    description_hash = digest(description)
    raw = output / 'benchmark.json'
    run.step(profile + '/benchmark', [*pinned, benchmark,
        '--benchmark_filter=' + args.filter, '--benchmark_min_time=' + args.min_time,
        '--benchmark_repetitions=' + str(args.repetitions),
        '--benchmark_enable_random_interleaving=false',
        '--benchmark_report_aggregates_only=false', '--benchmark_color=false',
        '--benchmark_out_format=json', '--benchmark_out=' + str(raw)])
    context, repetitions = export_samples(raw, output / 'samples.csv')
    if identities != {name: digest(binaries / name) for name in identities}:
        raise ValueError(f'{profile}: retained executable changed during measurement')
    if digest(description) != description_hash:
        raise ValueError(f'{profile}: case description changed during measurement')
    steps = [step for step in run.receipt['commands'] if step['name'].startswith(profile + '/')]
    save(output / 'context.json', {
        'profile': profile, 'march': PROFILES[profile], 'tune': args.tune,
        'cpu': args.cpu, 'source_digest': run.receipt['source_digest'],
        'description_sha256': description_hash, 'binary_sha256': identities,
        'existing_build_directory': existing, 'commands': steps,
        'build_context_sha256': {name: digest(output / name) for name in
            ['CMakeCache.txt', 'compile_commands.json', 'build.ninja', 'rules.ninja',
             '.ninja_log', 'commands.stdout']},
        'benchmark_context': context, 'individual_repetitions': repetitions,
        'timing_units': 'Google Benchmark time per iteration; item/preparation/reuse counters are study-owned',
    })
    return [f'{profile}/{name}' for name in
            ['samples.csv', 'context.json', 'checks.txt', 'extra-checks.txt', 'sizes.txt']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profiles', nargs='+', choices=PROFILES, required=True,
                        help='Build and run these profiles serially, in this order')
    parser.add_argument('--tune', choices=['generic', 'zen5', 'granite-rapids', 'neoverse-v2'],
                        default='generic', help='Microarchitectural tuning, independent of ISA profile')
    parser.add_argument('--cpu', type=int, help='Default: lowest allowed CPU')
    parser.add_argument('--jobs', type=int, default=int(os.environ.get('SIXDB_BUILD_JOBS', '1')))
    parser.add_argument('--filter', default='.', help='Google Benchmark case regex')
    parser.add_argument('--repetitions', type=int, default=5)
    parser.add_argument('--min-time', default='0.05s', help='Google Benchmark minimum, e.g. 0.05s or 1x')
    parser.add_argument('--workspace', type=Path, default=ROOT / 'build/workspaces/tuple-runtime')
    parser.add_argument('--output', type=Path, help='New run directory; defaults under build/experiments/')
    args = parser.parse_args()
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('Run on Linux; from macOS prefix with orb -m ubuntu')
    allowed = sorted(os.sched_getaffinity(0))
    args.cpu = allowed[0] if args.cpu is None else args.cpu
    if args.cpu not in allowed or args.jobs < 1 or args.repetitions < 1:
        parser.error('Choose an allowed CPU and positive jobs/repetitions')
    if (not re.fullmatch(r'(?:\d+(?:\.\d+)?s|[1-9]\d*x)', args.min_time) or
            (args.min_time.endswith('s') and float(args.min_time[:-1]) <= 0)):
        parser.error('--min-time expects positive seconds (0.05s) or iterations (1x)')
    if len(set(args.profiles)) != len(args.profiles):
        parser.error('Choose each profile once')
    arm = platform.machine().lower() in {'aarch64', 'arm64'}
    if any((profile == 'neon') != arm for profile in args.profiles):
        parser.error('Profiles must run on the matching host architecture')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = native_path(args.output or ROOT / 'build/experiments/tuple-runtime' / stamp)
    workspace = native_path(args.workspace)
    for path in [output, workspace]:
        if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / 'build'):
            parser.error('Keep repository-local outputs/workspaces under build/')
    if output.exists():
        parser.error(f'Output already exists: {output}')
    if output.is_relative_to(workspace) or workspace.is_relative_to(output):
        parser.error('Output and build workspace must be separate directories')
    config = {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}
    config.update(output=str(output), workspace=str(workspace), allowed_cpus=allowed)
    run = Run(ROOT, output, config, workspace=workspace)
    print(f'Run: {output}', flush=True)
    error = None
    try:
        run.step('git-head', ['git', 'rev-parse', 'HEAD'])
        run.step('compiler', ['clang++-21', '--version'])
        run.step('hardware', ['lscpu'], 'hardware.txt')
        selected = ['hardware.txt']
        for profile in args.profiles:
            selected.extend(run_profile(run, profile, args))
        run.compact(selected, [])
    except (Exception, KeyboardInterrupt) as exc:
        error = exc
    finally:
        error = run.finish(error)
    if error:
        print(f'FAILED: {error}; partial evidence: {output}', file=sys.stderr)
        return 130 if isinstance(error, KeyboardInterrupt) else 1
    retain = ['python3', 'workbench/tools/artifacts.py', 'retain', str(output),
              'workbench/spikes/tuple-layout/evidence/NAME']
    print(f'Complete: {output}\nTo retain this run:\n{shlex.join(retain)}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
