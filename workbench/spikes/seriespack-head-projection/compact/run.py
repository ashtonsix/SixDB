#!/usr/bin/env python3
"""One native.cpp overlay on a retained coherent worker build; no dispatch."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root = Path.cwd()
tune, profile = sys.argv[1:]
assert profile in ('avx2', 'avx512')
expected_library = {
    ('zen5', 'avx2'): '3dff85c3e2557325b57c0279736059702f7d18c6f3facdb8eafafc0939aeac69',
    ('granite-rapids', 'avx2'): '822247a1a102ae06bc130d963c9f3923e60dc85354081f3699227309789c9034',
    ('zen5', 'avx512'): '4e079e8e78ecf6e46154af4730a4f8adcc95be38e3d72e1b701f169b484afecc',
    ('granite-rapids', 'avx512'): '6539860725f988f080ffade80986a95e6191d1573f9be45dba490c94fee98d06',
}[tune, profile]
build = root / f'build/validation/seriespack/{tune}/{profile}/Release'
out = Path(os.environ['SIXDB_RESULTS']).resolve() / ('head16-public-' + profile)
out.mkdir(parents=True)
cpu = os.environ['SIXDB_CPU']
trial = root / 'workbench/spikes/ikea-composition/validation/diagnostics/head16_compact'
commands, inputs = [], {}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(label, argv, *, cwd=root, env=None):
    resource = out / (label + '.resources.json')
    full = ['taskset', '-c', cpu, '/usr/bin/time', '-q', '-o', str(resource),
            '-f', '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}',
            *map(str, argv)]
    with (out / (label + '.log')).open('w') as log:
        result = subprocess.run(full, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
    commands.append({'label': label, 'argv': full, 'cwd': str(cwd), 'returncode': result.returncode,
                     'resources': json.loads(resource.read_text())})
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    result.check_returncode()


sources = json.loads((trial / 'sources.json').read_text())['files']
assert all(digest(root / path) == sha for path, sha in sources.items())
library = build / 'ikea/libikea_seriespack.a'
assert digest(library) == expected_library
inputs[str(library)] = expected_library
assert digest(root / 'ikea/src/seriespack/native.cpp') == '1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da'
run('prepare-overlay', ['python3', trial / 'prepare_overlay.py', '--root', root, '--output', out / 'overlay'])
entries = json.loads((build / 'compile_commands.json').read_text())
entry, = [r for r in entries if r['file'].endswith('/ikea/src/seriespack/native.cpp')]
original = shlex.split(entry['command'])
if profile == 'avx2':
    assert '-march=x86-64-v3' in original and not any('gfni' in a or 'avx512' in a for a in original)
else:
    assert {'-march=x86-64-v4', '-mavx512vbmi', '-mavx512vbmi2', '-mgfni'} <= set(original)
candidate_object = out / 'native-candidate.o'
argv = original.copy()
argv[argv.index('-o') + 1] = str(candidate_object)
argv[argv.index('-c') + 1] = str(out / 'overlay/ikea/src/seriespack/native.cpp')
run('compile-native-candidate', argv, cwd=Path(entry['directory']))


def link(target, name, candidate):
    lines = subprocess.check_output(['ninja', '-t', 'commands', target], cwd=build, text=True).splitlines()
    line, = [line for line in lines if 'clang++' in line and ' -o ' in line and ' -c ' not in line]
    args = shlex.split(line)
    if args[:2] == [':', '&&']: args = args[2:]
    if args[-2:] == ['&&', ':']: args = args[:-2]
    assert not set(args) & {'&&', ';', '|', '>', '<'}
    assert 'ikea/libikea_seriespack.a' in args
    if candidate: args.insert(args.index('ikea/libikea_seriespack.a'), str(candidate_object))
    args[args.index('-o') + 1] = str(out / name)
    for arg in args:
        path = build / arg
        if arg.endswith(('.o', '.a')) and path.is_file(): inputs[str(path)] = digest(path)
    run('link-' + name, args, cwd=build)


common, skip = [], False
for arg in original:
    if skip:
        skip = False
        continue
    if arg in ('-o', '-MF', '-MT', '-MQ'): skip = True
    elif arg not in ('-c', '-MD', '-MMD', entry['file']): common.append(arg)
common.extend(['-I' + str(root), '-I' + str(root / 'ikea/include')])
check_object = out / 'check.o'
run('compile-guard', [*common, '-c', trial / 'check.cpp', '-o', check_object], cwd=Path(entry['directory']))
inputs[str(check_object)] = digest(check_object)
for variant, candidate in (('before', False), ('candidate', True)):
    run('link-' + variant + '-guard', [original[0], '-Wl,--gc-sections', check_object,
        *([candidate_object] if candidate else []), library, '-o', out / (variant + '-guard')])
    run(variant + '-guard', [out / (variant + '-guard'), profile])
    assert 'independent public placements' in (out / (variant + '-guard.log')).read_text()
    link('ikea_seriespack_bench', variant + '-bench', candidate)
for target, name in [('ikea_seriespack_physical_operations_check', 'public-check'),
                     ('ikea_seriespack_range_bounds_check', 'range-check')]:
    link(target, name, True)
    run(name, [out / name, '--target', profile])
(out / 'link-inputs.json').write_text(json.dumps(inputs, indent=2, sort_keys=True) + '\n')

# Exactly 61 affected descriptions, plus 16 unchanged endpoint controls.
selected = []
for k in range(16, 65):
    selected.append(f'local/k{k}/h16/u64/encode')
    if k - 16 in (1, 2, 3, 4, 5, 6, 7, 10, 12, 14, 15, 20):
        selected.append(f'striped/k{k}/h16/u64/encode')
selected.extend(['local/k16/h16/u16/encode', 'local/k17/h16/u32/encode',
    'striped/k23/h16/u32/encode', 'local/k32/h16/u32/encode',
    'local/k8/h8/u64/encode', 'local/k24/h8/u64/encode', 'local/k56/h8/u64/encode',
    'local/k64/h8/u64/encode', 'striped/k15/h8/u64/encode', 'striped/k23/h8/u64/encode',
    'local/k8/h0/u64/encode', 'local/k56/h0/u64/encode',
    'local/k16/h16/u64/decode', 'local/k32/h16/u64/decode', 'local/k64/h16/u64/decode',
    'striped/k23/h16/u64/decode'])
expected = {f'bulk/series/{profile}/' + name for name in selected}
assert len(expected) == 77
pattern = '^(' + '|'.join(re.escape(name) for name in sorted(expected)) + ')$'
(out / 'expected-cases.json').write_text(json.dumps(sorted(expected), indent=2) + '\n')
for count in (256, 8192, 65536):
    env = dict(os.environ, SERIESPACK_BULK_VALUES=str(count))
    for ordinal, variant in enumerate(('before', 'candidate', 'candidate', 'before'), 1):
        label = f'n{count}-{ordinal:02}-{variant}'
        samples = out / (label + '.json')
        run(label, [out / (variant + '-bench'), '--benchmark_filter=' + pattern,
            '--benchmark_min_time=0.05s', '--benchmark_repetitions=5',
            '--benchmark_enable_random_interleaving=false', '--benchmark_out_format=json',
            '--benchmark_out=' + str(samples)], env=env)
        rows = [r for r in json.loads(samples.read_text())['benchmarks'] if r.get('run_type') == 'iteration']
        assert len(rows) == len(expected) * 5 and not any(r.get('error_occurred') for r in rows)
        assert {r['run_name'] for r in rows} == expected
        assert all(r['logical_values'] == count for r in rows)

assert digest(library) == expected_library
assert all(digest(path) == sha for path, sha in inputs.items())
assert all(digest(root / path) == sha for path, sha in sources.items())
names = ('before-bench', 'candidate-bench', 'native-candidate.o', 'before-guard', 'candidate-guard',
         'public-check', 'range-check')
run('sizes', ['llvm-size-21', *[out / name for name in names]])
for name in ('before-bench', 'candidate-bench', 'native-candidate.o'):
    with (out / (name + '.asm')).open('w') as output:
        subprocess.run(['llvm-objdump-21', '--disassemble', '--no-show-raw-insn', out / name], stdout=output, check=True)
(out / 'receipt.json').write_text(json.dumps({'format': 1, 'status': 'passed', 'tune': tune, 'profile': profile,
    'scope': 'H16/u64 compact shared head projection on all 61 legal descriptions; unchanged payload and other carriers',
    'library_sha256': expected_library, 'sources_sha256': digest(trial / 'sources.json'),
    'main_iterations': 77 * 5 * 4 * 3, 'counts': [256, 8192, 65536], 'order': ['before', 'candidate', 'candidate', 'before'],
    'binaries': {name: digest(out / name) for name in names}}, indent=2) + '\n')
