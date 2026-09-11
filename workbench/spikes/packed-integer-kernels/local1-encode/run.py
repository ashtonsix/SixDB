#!/usr/bin/env python3
"""Pair one Local1/full-profile native object with the coherent public objects.

Run inside the existing captured worker workspace; hardware orchestration belongs
to the SeriesPack task. This script neither dispatches workers nor edits sources.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path.cwd()
tune = sys.argv[1]
expected_library = {
    'zen5': '4e079e8e78ecf6e46154af4730a4f8adcc95be38e3d72e1b701f169b484afecc',
    'granite-rapids': '6539860725f988f080ffade80986a95e6191d1573f9be45dba490c94fee98d06',
}[tune]
build = root / f'build/validation/seriespack/{tune}/avx512/Release'
out = Path(os.environ['SIXDB_RESULTS']).resolve() / 'local1-encode-public'
out.mkdir(parents=True)
cpu = os.environ['SIXDB_CPU']
probe = root / 'workbench/spikes/ikea-composition/validation'
trial = probe / 'diagnostics/local1_encode_region4'
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
    commands.append({'label': label, 'argv': full, 'cwd': str(cwd),
                     'returncode': result.returncode, 'resources': json.loads(resource.read_text())})
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    result.check_returncode()


sources = json.loads((trial / 'sources.json').read_text())['files']
assert all(digest(root / path) == sha for path, sha in sources.items())
library = build / 'ikea/libikea_seriespack.a'
assert digest(library) == expected_library
inputs[str(library)] = expected_library
assert digest(root / 'ikea/src/seriespack/native.cpp') == '1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da'
assert digest(probe / 'seriespack_bench.cpp') == '2f1b55dac92e625010727e8856543fd4ea2c722cb635cefb7bcb8bcf3fb0d98d'
run('prepare-overlay', ['python3', trial / 'prepare_overlay.py', '--root', root, '--output', out / 'overlay'])
entries = json.loads((build / 'compile_commands.json').read_text())
entry, = [r for r in entries if r['file'].endswith('/ikea/src/seriespack/native.cpp')]
original = shlex.split(entry['command'])
assert {'-march=x86-64-v4', '-mavx512vbmi', '-mavx512vbmi2', '-mgfni'} <= set(original)
candidate_object = out / 'native-candidate.o'
argv = original.copy()
argv.insert(1, '-I' + str(out / 'overlay'))
argv[argv.index('-o') + 1] = str(candidate_object)
run('compile-native-candidate', argv, cwd=Path(entry['directory']))


def link(target, name, candidate):
    lines = subprocess.check_output(['ninja', '-t', 'commands', target], cwd=build, text=True).splitlines()
    line, = [line for line in lines if 'clang++' in line and ' -o ' in line and ' -c ' not in line]
    args = shlex.split(line)
    if args[:2] == [':', '&&']:
        args = args[2:]
    if args[-2:] == ['&&', ':']:
        args = args[:-2]
    assert not set(args) & {'&&', ';', '|', '>', '<'}, args
    assert 'ikea/libikea_seriespack.a' in args
    if candidate:
        args.insert(args.index('ikea/libikea_seriespack.a'), str(candidate_object))
    args[args.index('-o') + 1] = str(out / name)
    for arg in args:
        path = build / arg
        if arg.endswith(('.o', '.a')) and path.is_file():
            inputs[str(path)] = digest(path)
    run('link-' + name, args, cwd=build)


# Use the coherent target's exact native compile flags for new diagnostic TUs.
# Build the public timing fixture once, then relink that identical object.
common = []
skip_next = False
for arg in original:
    if skip_next:
        skip_next = False
        continue
    if arg in ('-o', '-MF', '-MT', '-MQ'):
        skip_next = True
    elif arg not in ('-c', '-MD', '-MMD', entry['file']):
        common.append(arg)
assert not any(arg.endswith('native.cpp') for arg in common)
common.append('-I' + str(root / 'ikea/include'))
standalone_link = [original[0], '-Wl,--gc-sections']
for variant, candidate in (('before', False), ('candidate', True)):
    flags = common.copy()
    if candidate:
        flags.insert(1, '-I' + str(out / 'overlay'))
    obj = out / (variant + '-guard.o')
    run('compile-' + variant + '-guard', [*flags, '-c', trial / 'check.cpp', '-o', obj], cwd=Path(entry['directory']))
    run('link-' + variant + '-guard', [*standalone_link, obj, *([candidate_object] if candidate else []), library,
                                     '-o', out / (variant + '-guard')])
    run(variant + '-guard', [out / (variant + '-guard')])
    assert '7633 basis/projection/guard/stride/partial/public cases passed' in (out / (variant + '-guard.log')).read_text()

for target, name in [('ikea_seriespack_physical_operations_check', 'public-check'),
                     ('ikea_seriespack_range_bounds_check', 'range-check')]:
    link(target, name, True)
    run(name, [out / name, '--target', 'avx512'])

heads_object = out / 'heads-fixture.o'
run('compile-heads-fixture', [*common, '-c', trial / 'heads_bench.cpp', '-o', heads_object], cwd=Path(entry['directory']))
inputs[str(heads_object)] = digest(heads_object)
for variant, candidate in (('before', False), ('candidate', True)):
    run('link-' + variant + '-heads', [*standalone_link, heads_object, *([candidate_object] if candidate else []),
                                     library, '-o', out / (variant + '-heads')])
    link('ikea_seriespack_bench', variant + '-bench', candidate)
(out / 'link-inputs.json').write_text(json.dumps(inputs, indent=2, sort_keys=True) + '\n')

pattern = r'^bulk/series/avx512/local/(k1/h0|k2/h0|k9/h8|k17/h16)/u(8|16|32|64)/(encode|decode)$'
for count in (256, 8192, 65536):
    env = dict(os.environ, SERIESPACK_BULK_VALUES=str(count))
    for variant in ('before', 'candidate'):
        label = f'heads-n{count}-{variant}-check'
        run(label, [out / (variant + '-heads'), '--check-only'], env=env)
        rows = [json.loads(line) for line in (out / (label + '.log')).read_text().splitlines() if line.startswith('{')]
        assert rows[-1] == {'checks': 'passed', 'cases': 5}
    expected = None
    for ordinal, variant in enumerate(('before', 'candidate', 'candidate', 'before'), 1):
        label = f'n{count}-{ordinal:02}-{variant}'
        samples = out / (label + '.json')
        run(label, [out / (variant + '-bench'), '--benchmark_filter=' + pattern,
                    '--benchmark_min_time=0.05s', '--benchmark_repetitions=5',
                    '--benchmark_enable_random_interleaving=false', '--benchmark_out_format=json',
                    '--benchmark_out=' + str(samples)], env=env)
        rows = [r for r in json.loads(samples.read_text())['benchmarks'] if r.get('run_type') == 'iteration']
        assert rows and not any(r.get('error_occurred') for r in rows)
        names = {r['run_name'] for r in rows}
        assert len(names) == 16 and len(rows) == 80, (len(names), len(rows))
        assert all(r['logical_values'] == count for r in rows)
        if expected is None:
            expected = names
        assert expected == names
    for ordinal, variant in enumerate(('before', 'candidate', 'candidate', 'before'), 1):
        label = f'heads-n{count}-{ordinal:02}-{variant}'
        run(label, [out / (variant + '-heads')], env=env)
        rows = [json.loads(line) for line in (out / (label + '.log')).read_text().splitlines() if line.startswith('{')]
        timed = [r for r in rows if 'ns_per_value' in r]
        assert len(timed) == 25 and all(r['values'] == count and r['ns_per_value'] > 0 for r in timed)
        assert len({(r['head_bits'], r['head_placement']) for r in timed}) == 5

assert digest(library) == expected_library
assert all(digest(path) == sha for path, sha in inputs.items())
assert all(digest(root / path) == sha for path, sha in sources.items())
names = ('before-bench', 'candidate-bench', 'native-candidate.o', 'before-guard', 'candidate-guard',
         'public-check', 'range-check', 'heads-fixture.o', 'before-heads', 'candidate-heads')
run('sizes', ['llvm-size-21', *[out / name for name in names]])
for name in ('before-bench', 'candidate-bench', 'native-candidate.o', 'before-heads', 'candidate-heads'):
    with (out / (name + '.asm')).open('w') as output:
        subprocess.run(['llvm-objdump-21', '--disassemble', '--no-show-raw-insn', out / name], stdout=output, check=True)
(out / 'receipt.json').write_text(json.dumps({'format': 1, 'status': 'passed', 'tune': tune,
    'scope': 'Full-profile Local1/u8 four-region encode loop; unchanged public objects and baseline library',
    'library_sha256': expected_library, 'sources_sha256': digest(trial / 'sources.json'),
    'guard_cases_per_variant': 7633, 'main_iterations': 960, 'heads_iterations': 300,
    'binaries': {name: digest(out / name) for name in names}}, indent=2) + '\n')
