#!/usr/bin/env python3
"""Qualify and measure the final copied-projection predicate against its paired control.

Both arms share strict active-unit access, the final writer API correction and
factory-inferred harness types. Only filter.h differs. Complete after-core and
candidate-only lifetime/boundary checks precede the existing five-trial native
filter/block protocol. This controller launches no workers and edits no source.
"""
import argparse
import difflib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil

SPEC = importlib.util.spec_from_file_location('predicate_native', Path(__file__).with_name('predicate-native.py'))
predicate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(predicate)
native, ROOT, STUDY, HERE = predicate.native, predicate.ROOT, predicate.STUDY, predicate.HERE
INPUTS = Path('projection-inputs')
HEADER = predicate.HEADER
PARENT_SHA = '3b4f6992bc7f256675534d70c5140eb8e2b540ac1b1ae1dfea45542daf24e937'
MAP_SHA = {'before': '929b17889f350d24e2e740c052fc28411fcfaf589261c0216cc9b881b9f2cde6',
           'after': 'a4388c9a6695826bec2703d1da8ff1e56062b8c9f81bfeb489b047a2d0ef1d7d'}
CANDIDATE_SHA = '514e68275e3849bd58549b56f4f78fe81ee48ebce3df4cca830ba4091b568868'
SHARED = [HERE / 'projection-native.py', HERE / 'projection-native.sh',
          HERE / 'projection-check.cpp', HERE / 'predicate-native.py', HERE / 'blocks.cpp',
          STUDY / 'cohesion-v2-native.py', STUDY / 'cohesion-native/CMakeLists.txt']


def map_digest(value):
    return native.sha(json.dumps(value, sort_keys=True).encode())


def verify_tree(tree, files):
    for name, wanted in files.items():
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts, name
        assert native.sha((tree / name).read_bytes()) == wanted, (tree, name)
    # Generated build products and Python import caches are not source inputs.
    actual = {str(p.relative_to(tree)) for p in tree.rglob('*') if p.is_file()
              and p.relative_to(tree).parts[0] != 'build'
              and '__pycache__' not in p.relative_to(tree).parts and p.suffix not in ('.pyc', '.pyo')}
    assert actual == set(files), ('unexpected source files', tree, sorted(actual ^ set(files)))


def patch(inputs):
    return ''.join(difflib.unified_diff((inputs / 'before' / HEADER).read_text().splitlines(True),
        (inputs / 'after' / HEADER).read_text().splitlines(True),
        fromfile='before/' + HEADER, tofile='after/' + HEADER))


def verify(inputs):
    manifest = json.loads((inputs / 'manifest.json').read_text())
    assert manifest['study'] == 'prepared-predicate-native'  # Existing strict result-reader family.
    assert manifest['variant'] == 'copied-projection-final'
    assert manifest['parent_manifest_sha256'] == PARENT_SHA
    assert manifest['source_manifest_sha256'] == MAP_SHA
    assert manifest['candidate_sha256'] == CANDIDATE_SHA
    old, new = (manifest['source_sha256'][arm] for arm in ('before', 'after'))
    assert old.keys() == new.keys() and new[HEADER] == CANDIDATE_SHA
    assert [name for name in old if old[name] != new[name]] == [HEADER]
    for arm in ('before', 'after'):
        files = manifest['source_sha256'][arm]
        assert map_digest(files) == MAP_SHA[arm]
        verify_tree(inputs / arm, files)
    for name, record in manifest['parent_context']['shared_files'].items():
        assert old[name] == new[name], name
        # Copy-stage hashes precede the separately recorded neutral patches.
        # The complete pinned source maps admit the resulting final bytes.
        if name not in manifest['parent_context']['shared_patches']:
            assert old[name] == record['shared_sha256'], name
    for name in manifest['parent_context']['shared_patches']:
        assert old[name] == new[name], name
    assert set(manifest['shared_source_sha256']) == {str(p) for p in SHARED}
    for name, wanted in manifest['shared_source_sha256'].items():
        assert native.sha((ROOT / name).read_bytes()) == wanted, name
    blocks = str(HERE / 'blocks.cpp')
    assert old[blocks] == new[blocks] == manifest['shared_source_sha256'][blocks]
    body = (inputs / 'predicate.patch').read_bytes()
    assert native.sha(body) == manifest['patch_sha256'] and body.decode() == patch(inputs)
    assert manifest['timed_arms'] == ['before', 'after', 'duplicate']
    assert manifest['common_argv0'] == predicate.ARGV0
    assert manifest['trial_orders'] == [list(order) for order in predicate.ORDERS]
    return manifest


def manifest_for_pair(pair):
    """Verify immutable inputs and describe shared helpers without making a capture."""
    parent_file = pair / 'manifest.json'
    assert native.sha(parent_file.read_bytes()) == PARENT_SHA
    parent = json.loads(parent_file.read_text())
    assert parent['source_manifest_sha256'] == MAP_SHA
    assert set(parent['different_files']) == {HEADER}
    assert parent['source_sha256']['after'][HEADER] == CANDIDATE_SHA
    for arm in ('before', 'after'):
        assert map_digest(parent['source_sha256'][arm]) == MAP_SHA[arm]
        verify_tree(pair / arm, parent['source_sha256'][arm])
    assert parent['candidate_patch'] == patch(pair)
    shared = {str(p): native.sha((ROOT / p).read_bytes()) for p in SHARED}
    manifest = {'format': 1, 'study': 'prepared-predicate-native', 'variant': 'copied-projection-final',
        'scope': __doc__, 'parent_manifest_sha256': PARENT_SHA,
        'parent_context': {k: parent[k] for k in ('baseline', 'endpoint', 'access_scope',
                           'shared_files', 'shared_patches', 'runner_sha256')},
        'source_sha256': parent['source_sha256'], 'source_manifest_sha256': MAP_SHA,
        'only_changed_file': HEADER, 'candidate_sha256': CANDIDATE_SHA,
        'labels': {'before': 'shared strict-access/final-API control',
                   'after': 'same shared source plus copied-projection predicate<Byte,Rows>'},
        'patch_sha256': native.sha(parent['candidate_patch'].encode()),
        'shared_source_sha256': shared, 'timed_arms': ['before', 'after', 'duplicate'],
        'common_argv0': predicate.ARGV0, 'trial_orders': predicate.ORDERS,
        'worker_request': {'arm': 'c8g.4xlarge', 'x86': 'c8a.4xlarge', 'vcpus': 16,
                           'memory_gib': 32, 'build_jobs': 8}}
    return manifest, parent['candidate_patch']


def prepare(args):
    pair = args.source_pair.resolve()
    manifest, body = manifest_for_pair(pair)
    assert not args.metadata.exists(), 'choose fresh metadata directory'
    args.metadata.mkdir(parents=True)
    manifest_file, patch_file = args.metadata / 'manifest.json', args.metadata / 'predicate.patch'
    native.json_file(manifest_file, manifest)
    patch_file.write_text(body)
    result = native.tool_module('capture').capture(ROOT, args.capture.resolve(), replacements=[
        f'{INPUTS}/before={pair / "before"}', f'{INPUTS}/after={pair / "after"}',
        f'{INPUTS}/manifest.json={manifest_file}', f'{INPUTS}/predicate.patch={patch_file}'],
        expected=manifest['shared_source_sha256'])
    print(json.dumps({'capture': str(args.capture.resolve()), 'capture_source_digest': result['source_digest'],
                      'source_manifest_sha256': MAP_SHA, 'candidate_sha256': CANDIDATE_SHA}, indent=2))


def qualification_plan(args, inputs, out, profiles, commands):
    qualified = out / 'qualification'
    core = {'cwd': str(inputs / 'after'), 'log': str(qualified / 'core-command.log'),
        'command': ['env', 'SIXDB_RESULTS=' + str(qualified), 'SIXDB_CPU=' + str(args.cpu),
            'SIXDB_BUILD_JOBS=' + str(args.jobs), 'SIXDB_QUALIFY_CALLERS=0',
            'bash', str(STUDY / 'cohesion-v2-check.sh')]}
    fixtures = []
    for profile in profiles:
        item = next(b for b in commands['builds'] if b['profile'] == profile and b['arm'] == 'after')
        build, logs = Path(item['build']), qualified / profile / 'projection'
        fixtures.append({'profile': profile, 'build': str(build), 'logs': str(logs),
            'configure': item['configure'] + ['-DCOHESION_PROJECTION_CHECK=ON'],
            'compile': ['cmake', '--build', str(build), '--target', 'ikea_predicate_projection_check',
                        '-j', str(args.jobs)],
            'binary': str(build / 'ikea_predicate_projection_check'),
            'destination': str(logs / 'ikea_predicate_projection_check')})
    return {'core': core, 'fixtures': fixtures}


def run(args):
    inputs, out = (ROOT / args.inputs).resolve(), args.output.resolve()
    manifest = verify(inputs)
    host_profiles = ['neon'] if os.uname().machine in ('aarch64', 'arm64') else ['avx2', 'avx512']
    args.profile = args.profile or host_profiles
    commands = predicate.plan(args, inputs, out, args.profile)
    qualification = qualification_plan(args, inputs, out, args.profile, commands)
    if args.dry_run:
        print(json.dumps({'source_manifest_sha256': MAP_SHA, 'qualification': qualification,
                          'native': commands}, indent=2))
        return
    assert args.profile == host_profiles, 'qualify every execution profile on this native host'
    assert args.jobs == 8 and args.trials == 5 and args.filter_iterations == 300000 and args.min_time >= .1
    assert args.cpu in os.sched_getaffinity(0) and args.blocks_filter == '^blocks/'
    qualified = out / 'qualification'
    qualified.mkdir(parents=True, exist_ok=True)
    record = {'scope': 'After-only core/header and copied-projection lifetime/boundary qualification before timing.',
              'variant': manifest['variant'], 'profiles': host_profiles,
              'source_manifest_sha256': MAP_SHA, 'candidate_sha256': CANDIDATE_SHA,
              'source_manifest_file_sha256': native.sha((inputs / 'manifest.json').read_bytes()),
              'fixture_source_sha256': manifest['shared_source_sha256'][str(HERE / 'projection-check.cpp')],
              'runner_sha256': native.sha(Path(__file__).read_bytes()),
              'commands': [], 'core': {}, 'fixtures': [], 'complete': False}
    native.ACTIVE_RECEIPT, native.RECEIPT_PATH = record, out / 'projection-qualification.json'
    native.json_file(native.RECEIPT_PATH, record)
    def execute(command, cwd, log):
        native.run_command(command, cwd=Path(cwd), log=Path(log), receipt=record['commands'])
    core = qualification['core']
    execute(core['command'], core['cwd'], core['log'])
    for profile in host_profiles:
        selected = json.loads((qualified / (profile + '-selected.json')).read_text())
        headers = json.loads((qualified / (profile + '-headers.json')).read_text())['headers']
        assert len(selected) == 96 and len(headers) == 93
        assert all(h['returncode'] == 0 and h['sha256'] == manifest['source_sha256']['after'][h['path']]
                   for h in headers)
        check = qualified / (profile + '-check.log')
        assert re.search(r'100% tests passed, 0 tests failed out of 96', check.read_text())
        record['core'][profile] = {'checks': len(selected), 'public_headers': len(headers),
            'selected_receipt_sha256': native.sha((qualified / (profile + '-selected.json')).read_bytes()),
            'check_log_sha256': native.sha(check.read_bytes()),
            'header_receipt_sha256': native.sha((qualified / (profile + '-headers.json')).read_bytes())}
    for item in qualification['fixtures']:
        logs, build, destination = Path(item['logs']), Path(item['build']), Path(item['destination'])
        execute(item['configure'], ROOT, logs / 'configure.log')
        execute(item['compile'], ROOT, logs / 'build.log')
        for name in ('compile_commands.json', 'CMakeCache.txt'):
            shutil.copy2(build / name, logs / name)
        command = next(c['command'] for c in json.loads((build / 'compile_commands.json').read_text())
                       if c['file'].endswith('/projection-check.cpp'))
        flags = shlex.split(command)
        assert '-DIKEA_PROJECTION_CANDIDATE=1' in flags and not any(f.startswith('-DNDEBUG') for f in flags)
        assert {'-O3', '-g', '-std=c++23'} <= set(flags)
        march, tune, _ = native.PROFILES[item['profile']]
        clang_tune = {'zen5': 'znver5'}.get(tune, tune)
        assert '-march=' + march in flags and '-mtune=' + clang_tune in flags
        assert str(inputs / 'after' / 'ikea/include') in command
        shutil.copy2(item['binary'], destination)
        execute(['llvm-size-21', '-A', str(destination)], ROOT, logs / 'projection-size.txt')
        execute(['llvm-objdump-21', '-d', '-r', '--demangle', str(destination)], ROOT,
                logs / 'projection-disassembly.txt')
        execute(['taskset', '-c', str(args.cpu), str(destination)], inputs / 'after', logs / 'projection-check.txt')
        record['fixtures'].append({'profile': item['profile'], 'binary_sha256': native.sha(destination.read_bytes()),
            'bytes': destination.stat().st_size, 'compile_command': command,
            'disassembly_sha256': native.sha((logs / 'projection-disassembly.txt').read_bytes()),
            'run_log_sha256': native.sha((logs / 'projection-check.txt').read_bytes())})
    record['complete'] = True
    native.json_file(native.RECEIPT_PATH, record)
    predicate.run(args, verify_inputs=verify, runner_path=Path(__file__), scope=__doc__)
    path = out / 'receipt.json'
    receipt = json.loads(path.read_text())
    receipt['variant'] = manifest['variant']
    receipt['qualification'] = {'path': 'projection-qualification.json',
        'sha256': native.sha((out / 'projection-qualification.json').read_bytes()), 'complete': True}
    native.json_file(path, receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    prepare_parser = sub.add_parser('prepare')
    prepare_parser.add_argument('capture', type=Path)
    prepare_parser.add_argument('--source-pair', type=Path, required=True)
    prepare_parser.add_argument('--metadata', type=Path, required=True)
    run_parser = sub.add_parser('run')
    run_parser.add_argument('--inputs', type=Path, default=INPUTS)
    run_parser.add_argument('--output', type=Path, default=Path(os.environ.get('SIXDB_RESULTS', 'build/ikea-projection-native/results')))
    run_parser.add_argument('--cpu', type=int, default=int(os.environ.get('SIXDB_CPU', '0')))
    run_parser.add_argument('--jobs', type=int, default=int(os.environ.get('SIXDB_BUILD_JOBS', '8')))
    run_parser.add_argument('--profile', action='append', choices=native.PROFILES)
    run_parser.add_argument('--trials', type=int, default=5)
    run_parser.add_argument('--min-time', type=float, default=.1)
    run_parser.add_argument('--filter-iterations', type=int, default=300000)
    run_parser.add_argument('--blocks-filter', default='^blocks/')
    run_parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if args.mode == 'prepare':
        args.metadata = args.metadata.resolve()
        prepare(args)
    else:
        run(args)


if __name__ == '__main__':
    main()
