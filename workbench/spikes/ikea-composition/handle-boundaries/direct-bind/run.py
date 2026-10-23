#!/usr/bin/env python3
"""Build and measure complete preparation/control commands on native profiles.

All builds and independent checks finish before timing. Each family is one
linked executable; route filters select existing complete commands, not patched
leaves. The duplicate reruns identical shared-plan binary/arguments. Captured
sources and the worker archive retain the exact experiment, with no old-cohort
ratio correction or inferred cold-cache claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[5]
HERE = Path(__file__).resolve().parent
PROFILES = {'neon': ('armv8-a+simd', 'neoverse-v2'),
            'avx2': ('x86-64-v3', 'zen5'), 'avx512': ('znver5', 'zen5')}
HEADER_SHA = 'e634777e0b06e433066029e9ed4717ac5367177d7d537e59d41101c9a618381d'
ORDERS = [('construct_plan', 'direct', 'shared_plan', 'duplicate'),
          ('shared_plan', 'construct_plan', 'duplicate', 'direct'),
          ('duplicate', 'shared_plan', 'direct', 'construct_plan'),
          ('direct', 'duplicate', 'construct_plan', 'shared_plan'),
          ('duplicate', 'shared_plan', 'direct', 'construct_plan')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def execute(args, cwd, log, receipt):
    log.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(args, cwd=cwd, stdout=output, stderr=subprocess.STDOUT)
    receipt['commands'].append({'argv': args, 'cwd': str(cwd), 'log': str(log),
                                'returncode': result.returncode, 'seconds': time.monotonic() - start,
                                'log_sha256': sha(log)})
    save(receipt['path'], receipt)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}); inspect {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(os.environ.get('SIXDB_RESULTS', 'build/ikea-control-commands/results')))
    parser.add_argument('--cpu', type=int, default=int(os.environ.get('SIXDB_CPU', '8')))
    parser.add_argument('--jobs', type=int, default=int(os.environ.get('SIXDB_BUILD_JOBS', '8')))
    parser.add_argument('--profile', action='append', choices=PROFILES)
    parser.add_argument('--min-time', type=float, default=.1)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--series', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    assert not (out/'receipt.json').exists(), 'use fresh output; retain prior trials'
    assert args.cpu in os.sched_getaffinity(0)
    assert sha(ROOT/'ikea/include/ikea/tuplepack/filter.h') == HEADER_SHA
    sources = sorted([p for directory in ('ikea/include', 'ikea/src')
                      for p in (ROOT/directory).rglob('*') if p.is_file()])
    sources += [p for directory in (HERE, HERE.parent/'series-controls')
                if directory.exists() for p in directory.iterdir()
                if p.is_file() and p.suffix in ('.py', '.cpp', '.h', '.sh', '.txt')]
    sources += [ROOT/'ikea/test/tuplepack/predicate.cpp',
                ROOT/'ikea/test/seriespack/reference/physical.h']
    source_sha256 = {str(p.relative_to(ROOT)): sha(p) for p in sources}
    profiles = args.profile or (['neon'] if os.uname().machine == 'aarch64' else ['avx2', 'avx512'])
    targets = ['ikea_predicate_direct_commands', 'ikea_tuplepack_predicate_check']
    if args.series:
        targets.append('ikea_series_controls_commands')
    receipt = {'path': str(out/'receipt.json'), 'scope': __doc__, 'complete': False,
               'profiles': profiles, 'cpu': args.cpu, 'orders': ORDERS, 'commands': [],
               'runner_sha256': sha(Path(__file__)),
               'source_sha256': source_sha256,
               'predicate_header_sha256': sha(ROOT/'ikea/include/ikea/tuplepack/filter.h'),
               'compiler': subprocess.check_output(['clang++-21', '--version'], text=True),
               'binaries': {}, 'trials': [], 'limits': 'Already validated layouts; warm resident inputs; no allocation or layout construction timed. Not owner publication or old f153 regression qualification.'}
    save(out/'receipt.json', receipt)
    for profile in profiles:
        march, tune = PROFILES[profile]
        build = ROOT/'build/ikea-control-commands'/profile
        logs = out/profile
        configure = ['cmake', '-S', str(HERE), '-B', str(build), '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_DEBUG_INFO=lines',
                     '-DSIXDB_BENCHMARKS=tuplepack', '-DSIXDB_SPIKES=', '-DIKEA_COMPARISONS=OFF',
                     f'-DSIXDB_MARCH={march}', f'-DSIXDB_TUNE={tune}',
                     f'-DIKEA_CONTROL_SERIES={int(args.series)}']
        execute(configure, ROOT, logs/'configure.txt', receipt)
        execute(['cmake', '--build', str(build), '--target', *targets, '-j', str(args.jobs)],
                ROOT, logs/'build.txt', receipt)
        for name in ('compile_commands.json', 'CMakeCache.txt', 'build.ninja'):
            shutil.copy2(build/name, logs/name)
        binaries = {'direct': build/'ikea_predicate_direct_commands',
                    'predicate-check': build/'sixdb/ikea/ikea_tuplepack_predicate_check'}
        if args.series:
            binaries['series'] = build/'series-controls/ikea_series_controls_commands'
        receipt['binaries'][profile] = {}
        for family, source in binaries.items():
            dest = logs/source.name
            shutil.copy2(source, dest)
            receipt['binaries'][profile][family] = {'path': str(dest), 'sha256': sha(dest), 'bytes': dest.stat().st_size}
            execute(['taskset', '-c', str(args.cpu), str(dest), *([] if family == 'predicate-check' else ['--check-only'])],
                    ROOT, logs/(family+'-check.txt'), receipt)
            if family == 'series':
                execute([str(dest), '--facts'], ROOT, logs/'series-facts.json', receipt)
                assert json.loads((logs/'series-facts.json').read_text())['cases'] == 300
            if family != 'predicate-check':
                execute(['llvm-objdump-21', '-d', '-r', '--demangle', str(dest)], ROOT,
                        logs/(family+'-linked.txt'), receipt)
                execute(['llvm-size-21', '-A', str(dest)], ROOT, logs/(family+'-size.txt'), receipt)
        save(out/'receipt.json', receipt)
    # No compiler overlaps any timed native command, including another profile.
    if not args.check_only:
        for profile in profiles:
            binary = Path(receipt['binaries'][profile]['direct']['path'])
            for trial, order in enumerate(ORDERS):
                for route in order:
                    selected = 'shared_plan' if route == 'duplicate' else route
                    samples = out/profile/'samples.json'
                    log = out/profile/f'trial-{trial}-{route}.txt'
                    argv = ['taskset', '-c', str(args.cpu), 'bash', '-c', 'exec -a "$0" "$@"',
                            'ikea_direct_commands', str(binary),
                            '--benchmark_filter=^prepare/'+selected+'/',
                            f'--benchmark_min_time={args.min_time}s',
                            '--benchmark_out='+str(samples), '--benchmark_out_format=json']
                    execute(argv, ROOT, log, receipt)
                    result = json.loads(samples.read_text())
                    rows = [row for row in result['benchmarks'] if row.get('run_type') == 'iteration']
                    assert len(rows) == 27 and all(not row.get('error_occurred') for row in rows)
                    for row in rows:
                        assert row['time_unit'] == 'ns' and row['blocks'] == 64
                        count = int(row['name'].rsplit('/q', 1)[1])
                        assert row['queries_per_command'] == count
                        assert abs(row['cpu_time']/64 - row['ns_per_command']) <= max(1e-7, row['ns_per_command']*1e-10)
                        assert abs(row['cpu_time']/(64*count) - row['ns_per_query']) <= max(1e-7, row['ns_per_query']*1e-10)
                    dest = samples.with_name(f'trial-{trial}-{route}.json')
                    samples.replace(dest)
                    receipt['trials'].append({'profile': profile, 'trial': trial, 'route': route,
                                               'path': str(dest), 'sha256': sha(dest), 'cases': len(rows)})
                    save(out/'receipt.json', receipt)
            if args.series:
                binary = Path(receipt['binaries'][profile]['series']['path'])
                treatments = ['columns', 'shared-fresh', 'copied-retained', 'typed',
                              'columns-duplicate', 'copied-fresh', 'shared-retained', 'dispatch']
                for trial in range(5):
                    order = treatments[trial:] + treatments[:trial]
                    if trial % 2:
                        order = list(reversed(order))
                    for route in order:
                        samples = out/profile/'series-samples.json'
                        log = out/profile/f'series-{trial}-{route}.txt'
                        argv = ['taskset', '-c', str(args.cpu), 'bash', '-c', 'exec -a "$0" "$@"',
                                'ikea_series_controls', str(binary),
                                '--benchmark_filter=^series/(read/(point|range|read16)|initialize)/'+route+'/',
                                f'--benchmark_min_time={args.min_time}s',
                                '--benchmark_out='+str(samples), '--benchmark_out_format=json']
                        execute(argv, ROOT, log, receipt)
                        rows = [row for row in json.loads(samples.read_text())['benchmarks']
                                if row.get('run_type') == 'iteration']
                        expected = 12 if route == 'dispatch' else 48 if route in ('columns', 'columns-duplicate', 'typed') else 36
                        assert len(rows) == expected and all(not row.get('error_occurred') for row in rows)
                        assert all(row['time_unit'] == 'ns' for row in rows)
                        dest = samples.with_name(f'series-{trial}-{route}.json')
                        samples.replace(dest)
                        receipt['trials'].append({'profile': profile, 'trial': trial, 'family': 'series',
                                                   'route': route, 'path': str(dest), 'sha256': sha(dest),
                                                   'cases': len(rows), 'order': order})
                        save(out/'receipt.json', receipt)
    assert all(sha(ROOT/name) == wanted for name,wanted in source_sha256.items()), 'source changed during experiment'
    receipt['source_unchanged'] = True
    receipt['complete'] = True
    save(out/'receipt.json', receipt)
    print(json.dumps({'complete': True, 'receipt': str(out/'receipt.json'), 'trials': len(receipt['trials'])}))


if __name__ == '__main__':
    main()
