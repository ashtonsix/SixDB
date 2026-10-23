#!/usr/bin/env python3
"""Freeze and measure exact R4 versus its one-header prepared-predicate overlay.

The control is R4-after in both arms, never the historical f153 baseline or
later live core. Complete maintained filters and the shared block command
study own their oracles/operation grain. Every build and preflight finishes
before any timing; no compiler overlaps a timed trial.
"""
import argparse
import csv
import difflib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess

SPEC = importlib.util.spec_from_file_location('cohesion_native', Path(__file__).parents[1] / 'cohesion-v2-native.py')
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)
ROOT, STUDY = native.ROOT, native.STUDY
HERE = STUDY / 'handle-boundaries'
INPUTS = Path('predicate-inputs')
HEADER = 'ikea/include/ikea/tuplepack/filter.h'
R4_SOURCE_DIGEST = '288ff39e2c0d1e4396f34dac1b59262f9e49bfe15fa3b8928ad8c51fb855bd72'
CANDIDATE_SHA = '32ad56c26ed8f2d95a2dded82f394787c3c9e2cfdf550ffd826434ccd875b632'
BLOCKS_SHA = '27cab80683ef7180b4f4686de0b5ce5cfb2ee04b3060e8360dbcdd28a32aaec1'
TARGETS = {'filters': 'sixdb/workbench/spikes/filter-composition/filter_composition_extra',
           'oracles': 'sixdb/ikea/ikea_filters_check', 'blocks': 'ikea_predicate_blocks'}
SHARED = [HERE / 'predicate-native.py', HERE / 'predicate-native.sh', HERE / 'blocks.cpp',
          STUDY / 'cohesion-v2-native.py', STUDY / 'cohesion-native/CMakeLists.txt']
ORDERS = [('before', 'after', 'duplicate'), ('after', 'duplicate', 'before'),
          ('duplicate', 'before', 'after'), ('duplicate', 'after', 'before'),
          ('after', 'before', 'duplicate')]
ARGV0 = {family: 'ikea_predicate_' + family for family in TARGETS}


def prepare(args):
    parent = json.loads(args.source_manifest.read_text())
    hashes = parent['source_sha256']['after']
    assert native.sha(json.dumps(hashes, sort_keys=True).encode()) == R4_SOURCE_DIGEST
    assert parent['source_manifest_sha256']['after'] == R4_SOURCE_DIGEST
    candidate = args.candidate.read_bytes()
    assert native.sha(candidate) == CANDIDATE_SHA
    assert hashes[HEADER] != CANDIDATE_SHA
    shared = {str(p): native.sha((ROOT / p).read_bytes()) for p in SHARED}
    assert shared[str(HERE / 'blocks.cpp')] == BLOCKS_SHA
    assert not args.scratch.exists(), 'choose a fresh scratch directory'
    for arm in ('before', 'after'):
        tree = args.scratch / arm
        for name, wanted in hashes.items():
            source = args.source / name
            body = source.read_bytes()
            assert native.sha(body) == wanted, name
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(candidate if arm == 'after' and name == HEADER else body)
            target.chmod(source.stat().st_mode & 0o777)
    after = {**hashes, HEADER: CANDIDATE_SHA}
    patch = ''.join(difflib.unified_diff((args.scratch / 'before' / HEADER).read_text().splitlines(True),
        candidate.decode().splitlines(True), fromfile='r4-after/' + HEADER, tofile='cached-tight-address/' + HEADER))
    patch_path = args.scratch / 'predicate.patch'
    patch_path.write_text(patch)
    manifest = {'format': 1, 'study': 'prepared-predicate-native', 'scope': __doc__,
        'r4_parent_manifest_sha256': native.sha(args.source_manifest.read_bytes()),
        'r4_after_source_manifest_sha256': R4_SOURCE_DIGEST,
        'r4_after_source_head': parent['after_source_head'],
        'labels': {'before': 'exact frozen R4 after', 'after': 'same exact R4 after + cached-tight-address filter.h'},
        'source_sha256': {'before': hashes, 'after': after},
        'source_manifest_sha256': {arm: native.sha(json.dumps(value, sort_keys=True).encode())
                                   for arm, value in (('before', hashes), ('after', after))},
        'only_changed_file': HEADER, 'candidate_sha256': CANDIDATE_SHA,
        'patch_sha256': native.sha(patch.encode()), 'shared_source_sha256': shared,
        'timed_arms': ['before', 'after', 'duplicate'], 'common_argv0': ARGV0,
        'trial_orders': ORDERS,
        'worker_request': {'arm': 'c8g.4xlarge', 'x86': 'c8a.4xlarge', 'vcpus': 16, 'memory_gib': 32, 'build_jobs': 8}}
    native.json_file(args.scratch / 'manifest.json', manifest)
    capture = native.tool_module('capture')
    result = capture.capture(ROOT, args.capture.resolve(), replacements=[
        f'{INPUTS}/before={args.scratch / "before"}', f'{INPUTS}/after={args.scratch / "after"}',
        f'{INPUTS}/manifest.json={args.scratch / "manifest.json"}', f'{INPUTS}/predicate.patch={patch_path}'], expected=shared)
    print(json.dumps({'capture': str(args.capture.resolve()), 'capture_source_digest': result['source_digest'],
        'source_manifest_sha256': manifest['source_manifest_sha256'], 'candidate_sha256': CANDIDATE_SHA}, indent=2))


def verify(inputs, *, shared=True):
    manifest = json.loads((inputs / 'manifest.json').read_text())
    assert manifest['study'] == 'prepared-predicate-native'
    assert manifest['r4_after_source_manifest_sha256'] == R4_SOURCE_DIGEST
    assert manifest['candidate_sha256'] == CANDIDATE_SHA
    old, new = manifest['source_sha256']['before'], manifest['source_sha256']['after']
    assert manifest['source_manifest_sha256']['before'] == R4_SOURCE_DIGEST
    assert old.keys() == new.keys() and new[HEADER] == CANDIDATE_SHA
    assert sorted(k for k in old.keys() | new.keys() if old.get(k) != new.get(k)) == [HEADER]
    for arm, files in manifest['source_sha256'].items():
        assert native.sha(json.dumps(files, sort_keys=True).encode()) == manifest['source_manifest_sha256'][arm]
        for name, wanted in files.items():
            assert native.sha((inputs / arm / name).read_bytes()) == wanted, (arm, name)
    assert set(manifest['shared_source_sha256']) == {str(p) for p in SHARED}
    assert manifest['shared_source_sha256'][str(HERE / 'blocks.cpp')] == BLOCKS_SHA
    if shared:
        for name, wanted in manifest['shared_source_sha256'].items():
            assert native.sha((ROOT / name).read_bytes()) == wanted, name
    assert native.sha((inputs / 'predicate.patch').read_bytes()) == manifest['patch_sha256']
    return manifest


def refresh(args):
    """Derive a fresh capture, reusing immutable paired inputs without recopying scratch."""
    manifest = verify(args.inputs, shared=False)
    assert not args.metadata.exists(), 'choose fresh metadata path'
    manifest['shared_source_sha256'] = {str(p): native.sha((ROOT / p).read_bytes()) for p in SHARED}
    assert manifest['shared_source_sha256'][str(HERE / 'blocks.cpp')] == BLOCKS_SHA
    manifest['timed_arms'] = ['before', 'after', 'duplicate']
    manifest['duplicate'] = 'Copy exact before ELF bytes after compilation; source remains before; no third source treatment.'
    manifest['common_argv0'] = ARGV0
    manifest['trial_orders'] = ORDERS
    manifest['scope'] = __doc__
    native.json_file(args.metadata, manifest)
    result = native.tool_module('capture').capture(ROOT, args.capture.resolve(), replacements=[
        f'{INPUTS}/before={args.inputs / "before"}', f'{INPUTS}/after={args.inputs / "after"}',
        f'{INPUTS}/manifest.json={args.metadata}', f'{INPUTS}/predicate.patch={args.inputs / "predicate.patch"}'],
        expected=manifest['shared_source_sha256'])
    print(json.dumps({'capture': str(args.capture.resolve()), 'capture_source_digest': result['source_digest'],
        'source_manifest_sha256': manifest['source_manifest_sha256'], 'candidate_sha256': CANDIDATE_SHA,
        'timed_arms': manifest['timed_arms'], 'common_argv0': ARGV0, 'trial_orders': ORDERS}, indent=2))


def pinned(binary, arguments, cpu, family):
    # bash exec replaces the wrapper; argv[0] and user arguments are identical
    # across arms. The executable pathname is retained separately in receipt.
    return ['taskset', '-c', str(cpu), 'bash', '-c', 'exec -a "$0" "$@"',
            ARGV0[family], str(binary), *arguments]


def plan(args, inputs, out, profiles):
    builds, paths, preflight, timings = [], {}, [], []
    for profile in profiles:
        march, tune, extra = native.PROFILES[profile]
        paths[profile] = {}
        for arm in ('before', 'after'):
            source = inputs / arm
            build = ROOT / 'build/ikea-predicate-native' / profile / arm
            logs = out / profile / arm
            flags = ['-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_DEBUG_INFO=lines',
                '-DSIXDB_BENCHMARKS=tuplepack', '-DSIXDB_SPIKES=filter-composition', '-DIKEA_COMPARISONS=OFF',
                f'-DSIXDB_MARCH={march}', f'-DSIXDB_TUNE={tune}', f'-DCMAKE_CXX_FLAGS={extra}']
            builds.append({'profile': profile, 'arm': arm, 'build': str(build), 'logs': str(logs),
                'configure': ['cmake', '-S', str(ROOT / STUDY / 'cohesion-native'), '-B', str(build), '-G', 'Ninja',
                    '-DCOHESION_SOURCE_ROOT=' + str(source), '-DCOHESION_BEFORE=0', *flags],
                'compile': ['cmake', '--build', str(build), '--target', *[Path(p).name for p in TARGETS.values()], '-j', str(args.jobs)]})
            paths[profile][arm] = {family: logs / Path(relative).name for family, relative in TARGETS.items()}
        paths[profile]['duplicate'] = {family: out / profile / 'duplicate' / Path(relative).name
                                       for family, relative in TARGETS.items()}
        for arm in ('before', 'after', 'duplicate'):
            source = inputs / ('before' if arm == 'duplicate' else arm)
            logs, p = out / profile / arm, paths[profile][arm]
            for family, command in (
                ('filters-check', pinned(p['filters'], ['--check'], args.cpu, 'filters')),
                ('ikea-filters-check', pinned(p['oracles'], [], args.cpu, 'oracles')),
                ('blocks-check', pinned(p['blocks'], ['--check-only'], args.cpu, 'blocks')),
                ('blocks-cases', pinned(p['blocks'], ['--benchmark_list_tests=true', '--benchmark_filter=' + args.blocks_filter], args.cpu, 'blocks'))):
                preflight.append({'profile': profile, 'arm': arm, 'cwd': str(source), 'log': str(logs / (family + '.txt')),
                                  'command': command})
    # All profiles and arms are built/validated before this separate phase.
    for profile in profiles:
        for trial in range(args.trials):
            order = ORDERS[trial % len(ORDERS)]
            for arm in order:
                logs, p = out / profile / arm, paths[profile][arm]
                # Same cwd and user argv in all timed arms. Only the actual
                # executable path differs; argv[0] is replaced by pinned().
                source = out / 'runtime'
                common_result = source / 'blocks-result.json'
                timings.append({'profile': profile, 'arm': arm, 'trial': trial, 'family': 'filters',
                    'cwd': str(source), 'log': str(logs / f'filters-trial-{trial}.csv'),
                    'stderr_log': str(logs / f'filters-trial-{trial}.stderr.txt'),
                    'command': pinned(p['filters'], [str(args.filter_iterations), '1'], args.cpu, 'filters')})
                timings.append({'profile': profile, 'arm': arm, 'trial': trial, 'family': 'blocks',
                    'cwd': str(source), 'log': str(logs / f'blocks-trial-{trial}.log'),
                    'result': str(logs / f'blocks-trial-{trial}.json'),
                    'result_source': str(common_result),
                    'command': pinned(p['blocks'], ['--benchmark_filter=' + args.blocks_filter,
                        f'--benchmark_min_time={args.min_time}s', '--benchmark_repetitions=1',
                        '--benchmark_enable_random_interleaving=false', '--benchmark_out_format=json',
                        '--benchmark_out=' + str(common_result)], args.cpu, 'blocks')})
    copies = [{'profile': profile, 'family': family, 'source': str(paths[profile]['before'][family]),
               'destination': str(paths[profile]['duplicate'][family])}
              for profile in profiles for family in TARGETS]
    return {'builds': builds, 'duplicate_copies': copies, 'preflight': preflight, 'timings': timings}


def run(args, *, verify_inputs=verify, runner_path=None, scope=None):
    inputs, out = (ROOT / args.inputs).resolve(), args.output.resolve()
    manifest = verify_inputs(inputs)
    assert manifest['timed_arms'] == ['before', 'after', 'duplicate']
    assert manifest['common_argv0'] == ARGV0 and manifest['trial_orders'] == [list(order) for order in ORDERS]
    profiles = args.profile or (['neon'] if os.uname().machine == 'aarch64' else ['avx2', 'avx512'])
    commands = plan(args, inputs, out, profiles)
    if args.dry_run:
        print(json.dumps({'source_manifest_sha256': manifest['source_manifest_sha256'], **commands}, indent=2))
        return
    assert args.cpu in os.sched_getaffinity(0)
    assert args.jobs == 8 and args.trials == 5 and args.filter_iterations == 300000 and args.min_time >= 0.1
    out.mkdir(parents=True, exist_ok=True)
    receipt = {'scope': scope or __doc__, 'source_manifest_sha256': manifest['source_manifest_sha256'],
        'candidate_sha256': manifest['candidate_sha256'],
        'runner_sha256': native.sha(Path(runner_path or __file__).read_bytes()),
        'timed_arms': manifest['timed_arms'], 'common_argv0': ARGV0, 'trial_orders': ORDERS,
        'shared_source_sha256': manifest['shared_source_sha256'], 'profiles': profiles, 'trials': args.trials,
        'cpu': args.cpu, 'allowed_cpus': sorted(os.sched_getaffinity(0)), 'build_jobs': args.jobs,
        'filter_iterations': args.filter_iterations, 'filter_repeats': 1, 'blocks_minimum_time_seconds': args.min_time,
        'blocks_filter': args.blocks_filter, 'commands': [], 'binaries': [], 'case_sets': {},
        'limits': 'Warm complete commands; preparation-inclusive block modes and serial filter traces stay separate. No universal performance or cold-memory claim. Linked placement remains part of native results. Common argv0 does not disable ASLR or equalize every loader/environment effect; duplicate-before ELF is a measured negative control, not a correction factor.'}
    native.ACTIVE_RECEIPT, native.RECEIPT_PATH = receipt, out / 'receipt.json'
    native.json_file(native.RECEIPT_PATH, receipt)
    shutil.copy2(inputs / 'manifest.json', out / 'source-manifest.json')
    for name, command in (('compiler', ['clang++-21', '--version']), ('lscpu', ['lscpu'])):
        (out / (name + '.txt')).write_text(subprocess.check_output(command, text=True))
    (out / 'cpuinfo.txt').write_text(Path('/proc/cpuinfo').read_text())
    def execute(command, cwd, log, stderr_log=None):
        native.run_command(command, cwd=Path(cwd), log=Path(log), stderr_log=Path(stderr_log) if stderr_log else None,
                           receipt=receipt['commands'])
    for item in commands['builds']:
        logs, build = Path(item['logs']), Path(item['build'])
        execute(item['configure'], ROOT, logs / 'configure.log')
        execute(item['compile'], ROOT, logs / 'build.log')
        for name in ('compile_commands.json', 'CMakeCache.txt'):
            shutil.copy2(build / name, logs / name)
        for family, relative in TARGETS.items():
            destination = logs / Path(relative).name
            shutil.copy2(build / relative, destination)
            receipt['binaries'].append({'profile': item['profile'], 'arm': item['arm'], 'family': family,
                'sha256': native.sha(destination.read_bytes()), 'bytes': destination.stat().st_size})
            execute(['llvm-size-21', '-A', str(destination)], ROOT, logs / (family + '-size.txt'))
            execute(['llvm-objdump-21', '-d', '-r', '--demangle', str(destination)], ROOT, logs / (family + '-disassembly.txt'))
    for item in commands['duplicate_copies']:
        source, destination = Path(item['source']), Path(item['destination'])
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        before = next(row for row in receipt['binaries'] if row['profile'] == item['profile'] and
                      row['arm'] == 'before' and row['family'] == item['family'])
        assert native.sha(destination.read_bytes()) == before['sha256']
        receipt['binaries'].append({**before, 'arm': 'duplicate', 'copied_from': str(source), 'no_compilation': True})
        for suffix in ('-size.txt', '-disassembly.txt'):
            shutil.copy2(source.parent / (item['family'] + suffix), destination.parent / (item['family'] + suffix))
        for name in ('compile_commands.json', 'CMakeCache.txt'):
            shutil.copy2(source.parent / name, destination.parent / name)
    for item in commands['preflight']:
        execute(item['command'], item['cwd'], item['log'])
        if item['log'].endswith('blocks-cases.txt'):
            cases = Path(item['log']).read_text().splitlines()
            assert cases and all(re.match(r'^blocks/', c) for c in cases), cases
            receipt['case_sets'][item['profile'] + '/' + item['arm'] + '/blocks-listed'] = cases
    for profile in profiles:
        assert receipt['case_sets'][profile + '/before/blocks-listed'] == receipt['case_sets'][profile + '/after/blocks-listed']
        assert receipt['case_sets'][profile + '/before/blocks-listed'] == receipt['case_sets'][profile + '/duplicate/blocks-listed']
    receipt['preflight_complete'] = True
    native.json_file(native.RECEIPT_PATH, receipt)
    (out / 'runtime').mkdir(exist_ok=True)
    for item in commands['timings']:
        execute(item['command'], item['cwd'], item['log'], item.get('stderr_log'))
        if item['family'] == 'filters':
            rows = list(csv.DictReader(Path(item['log']).open()))
            assert rows and all(math.isfinite(float(r['ns_window'])) and float(r['ns_window']) > 0 for r in rows)
            cases = sorted('/'.join(r[k] for k in ('workload', 'width_or_cases', 'op', 'active', 'candidate', 'metric')) for r in rows)
            assert len(cases) == 360, ('incomplete R4 filter coverage', len(cases))
            assert len(cases) == len(set(cases)), 'unexpected repeated filter cases'
        else:
            shutil.move(item['result_source'], item['result'])
            rows = json.loads(Path(item['result']).read_text())['benchmarks']
            assert rows and not any(r.get('error_occurred') for r in rows)
            assert all(math.isfinite(float(r['cpu_time'])) and float(r['cpu_time']) > 0 for r in rows)
            cases = sorted(r['name'] for r in rows)
            assert len(cases) == len(set(cases))
        key = item['profile'] + '/' + item['family']
        if key in receipt['case_sets']:
            assert receipt['case_sets'][key] == cases, 'case coverage differs between arms/trials'
        receipt['case_sets'][key] = cases
        native.json_file(native.RECEIPT_PATH, receipt)
        native.json_file(out / 'progress.json', {k: item[k] for k in ('profile', 'trial', 'arm', 'family')})
    receipt['complete'] = True
    native.json_file(native.RECEIPT_PATH, receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('capture', type=Path)
    p.add_argument('--scratch', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--source-manifest', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p = sub.add_parser('refresh')
    p.add_argument('capture', type=Path)
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--metadata', type=Path, required=True)
    p = sub.add_parser('run')
    p.add_argument('--inputs', type=Path, default=INPUTS)
    p.add_argument('--output', type=Path, default=Path(os.environ.get('SIXDB_RESULTS', 'build/ikea-predicate-native/results')))
    p.add_argument('--cpu', type=int, default=int(os.environ.get('SIXDB_CPU', '0')))
    p.add_argument('--jobs', type=int, default=int(os.environ.get('SIXDB_BUILD_JOBS', '8')))
    p.add_argument('--profile', action='append', choices=native.PROFILES)
    p.add_argument('--trials', type=int, default=5)
    p.add_argument('--min-time', type=float, default=0.1)
    p.add_argument('--filter-iterations', type=int, default=300000)
    p.add_argument('--blocks-filter', default='^blocks/')
    p.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if args.mode == 'prepare':
        args.source, args.source_manifest, args.candidate, args.scratch = (
            getattr(args, key).resolve() for key in ('source', 'source_manifest', 'candidate', 'scratch'))
        prepare(args)
    elif args.mode == 'refresh':
        args.inputs, args.metadata = args.inputs.resolve(), args.metadata.resolve()
        refresh(args)
    else:
        run(args)


if __name__ == '__main__':
    main()
