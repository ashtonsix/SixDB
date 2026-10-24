#!/usr/bin/env python3
"""Qualify pinned native Calico controls; no timings, workers or sibling checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[5]
HERE = Path(__file__).resolve().parent
PROVIDER = ROOT / 'workbench/peers/frame-calico.cmake'
MANIFEST = json.loads((HERE / 'source.json').read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_archive(path):
    assert path.stat().st_size == MANIFEST['archive_bytes']
    assert sha(path) == MANIFEST['archive_sha256']
    actual = {}
    with tarfile.open(path) as archive:
        for member in archive:
            if member.isdir():
                continue
            assert member.isfile() and member.name.startswith('calico/')
            name = member.name.removeprefix('calico/')
            assert name not in actual and '..' not in Path(name).parts
            actual[name] = hashlib.sha256(archive.extractfile(member).read()).hexdigest()
    assert actual == MANIFEST['files_sha256']


def prepare(root):
    archive = root / 'sources' / f"calico-{MANIFEST['revision']}.tar.gz"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        with tempfile.TemporaryDirectory(prefix='recover-', dir=root) as temporary:
            destination = Path(temporary) / 'bundle'
            subprocess.run([sys.executable, str(ROOT / 'workbench/tools/artifacts.py'), 'fetch',
                str(HERE / 'source-artifact.json'), str(destination)], check=True)
            verify_archive(destination / 'calico.tar.gz')
            assert json.loads((destination / 'source.json').read_text()) == MANIFEST
            shutil.copyfile(destination / 'calico.tar.gz', archive)
    verify_archive(archive)
    return archive


def command(argv, output, *, env=None):
    with output.open('w') as log:
        result = subprocess.run([str(x) for x in argv], cwd=ROOT, env=env,
            stdout=log, stderr=subprocess.STDOUT)
    assert result.returncode == 0, f"exit {result.returncode}: {argv}; see {output}"
    return dict(argv=[str(x) for x in argv], exit=result.returncode, log=output.name, sha256=sha(output))


def verify_sources(build):
    source = build / '_deps' / f"frame-calico-{MANIFEST['revision']}" / 'calico'
    assert {str(p.relative_to(source)): sha(p) for p in source.rglob('*') if p.is_file()} == MANIFEST['files_sha256']
    return source


def linked_proof(binary, output):
    nm = subprocess.check_output(['llvm-nm-21', '-S', '--format=posix', str(binary)], text=True)
    symbols = {}
    for line in nm.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[1] in {'T', 'W'}:
            symbols[fields[0]] = dict(address=int(fields[2], 16), bytes=int(fields[3], 16))
    selected = {}
    for key, needle in [
        ('signed_wrapper', '_ZNK14calico_control12signed_image3sum'),
        ('unsigned_wrapper', '_ZNK14calico_control14unsigned_image3sum'),
        ('container_sum', '_ZNK6engine19typebound_leaf_view3sum'),
        ('bound_raw_sum', '_ZN6engine22raw_farray_sum_trustedERKNS_19raw_farray_sum_plan'),
        ('residual_sum', '_ZN6engine23raw_farray_residual_sum')]:
        names = [name for name in symbols if name.startswith(needle)]
        assert len(names) == 1, (key, names)
        name = names[0]
        s = symbols[name]
        argv = ['llvm-objdump-21', '-d', '--demangle', '--no-show-raw-insn',
            f"--start-address={s['address']}", f"--stop-address={s['address']+s['bytes']}", str(binary)]
        assembly = subprocess.check_output(argv, text=True)
        path = output / f'{key}.asm'
        path.write_text(assembly)
        selected[key] = dict(symbol=name, **s, argv=argv, assembly=path.name, sha256=sha(path))
    signed = (output / 'signed_wrapper.asm').read_text()
    unsigned = (output / 'unsigned_wrapper.asm').read_text()
    container = (output / 'container_sum.asm').read_text()
    raw = (output / 'bound_raw_sum.asm').read_text()
    residual = (output / 'residual_sum.asm').read_text()
    assert 'engine::raw_farray_sum_trusted' in signed
    assert 'engine::typebound_leaf_view::sum' in unsigned
    assert 'engine::raw_farray_sum_trusted' in container
    assert 'engine::raw_farray_residual_sum' in raw
    neon = sorted(set(re.findall(r'\b(?:uaddlp|uadalp|uaddlv|addv)\b', residual)))
    assert neon, 'No native NEON reduction in linked residual path'
    dynamic = subprocess.check_output(['ldd', str(binary)], text=True)
    (output / 'linked-libraries.txt').write_text(dynamic)
    assert binary.read_bytes()[:6] == b'\x7fELF\x02\x01'
    return dict(symbols=selected, reduction_mnemonics=neon,
        linked_libraries_sha256=sha(output / 'linked-libraries.txt'),
        scope='Actual called wrappers link the bound SUM and native NEON residual branches; no dynamic instruction trace or timing claim.')


def functional(binary, output, stem, args, env):
    argv = ['taskset', '-c', str(args.cpu), str(binary), 'all-safe-ranges']
    with (output / f'{stem}.json').open('w') as stdout, (output / f'{stem}.log').open('w') as stderr:
        result = subprocess.run(argv, stdout=stdout, stderr=stderr, env=env)
    assert result.returncode == 0, f"See {stem}.log: exit {result.returncode}"
    witness = json.loads((output / f'{stem}.json').read_text())
    assert witness['pass'] and witness['cases'] >= 1638 and witness['sum_calls'] == 6649
    assert witness['bounded_range_endpoints'] and witness['bounded_range_omissions'] > 0
    assert witness['guarded_calls'] > 7000 and witness['refusals'] >= 1000
    return dict(argv=argv, exit=result.returncode, witness=witness,
        stdout_sha256=sha(output / f'{stem}.json'), stderr_sha256=sha(output / f'{stem}.log'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, default=ROOT / 'build/frame-helper/calico')
    parser.add_argument('--prepare', action='store_true', help='Recover and verify only the source archive')
    parser.add_argument('--cpu', type=int, default=8)
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'aarch64', 'Use native pinned Linux AArch64'
    root = args.build_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    archive = prepare(root)
    if args.prepare:
        print(json.dumps(dict(source_archive=str(archive), sha256=sha(archive), files=len(MANIFEST['files_sha256']))))
        return
    assert args.cpu in os.sched_getaffinity(0)
    output = root / 'evidence'
    output.mkdir(exist_ok=True)
    paths = [PROVIDER] + [HERE / name for name in ['CMakeLists.txt', 'control.h', 'control.cpp', 'check.cpp', 'run.py', 'source.json', 'source-artifact.json']]
    inputs = {str(p.relative_to(ROOT)): sha(p) for p in paths}
    compiler = subprocess.check_output(['clang++-21', '--version'], text=True)
    assert 'clang version 21.1.8' in compiler
    profiles = []
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
    for profile in ['normal', 'sanitize']:
        build = root / profile
        commands = [command(['cmake', '-S', HERE, '-B', build, '-G', 'Ninja',
            '-DSIXDB_FRAME_CALICO_SANITIZE=' + ('ON' if profile == 'sanitize' else 'OFF'),
            f'-DWORKBENCH_FRAME_CALICO_ARCHIVE={archive}'], output / f'{profile}-configure.log')]
        verify_sources(build)
        commands.append(command(['taskset', '-c', args.cpu, 'cmake', '--build', build,
            '--target', 'frame_calico_check', '-j1'], output / f'{profile}-build.log'))
        binary = build / 'frame_calico_check'
        witness = functional(binary, output, profile, args, env)
        source = verify_sources(build)
        compiles = json.loads((build / 'compile_commands.json').read_text())
        assert len(compiles) == 2
        for entry in compiles:
            flags = shlex.split(entry['command'])
            assert '-march=armv8-a+simd' in flags and '-mtune=neoverse-v2' in flags
            assert '-std=c++23' in flags or '-std=gnu++23' in flags
            assert ('-fsanitize=address,undefined' in flags) == (profile == 'sanitize')
        deps = subprocess.check_output(['ninja', '-C', str(build), '-t', 'deps'], text=True)
        actual = {}
        for line in deps.splitlines():
            path = Path(line.strip())
            if path.is_absolute() and path.is_relative_to(source) and path.is_file():
                actual[str(path.relative_to(source))] = sha(path)
        assert len(actual) >= 15 and all(MANIFEST['files_sha256'].get(name) == value for name, value in actual.items())
        profiles.append(dict(profile=profile, commands=commands, binary_sha256=sha(binary),
            source_files_verified=len(MANIFEST['files_sha256']), compiled_upstream_files=actual,
            compile_commands=compiles, functional=witness))
    binary = root / 'sanitize/frame_calico_check'
    argv = ['taskset', '-c', str(args.cpu), str(binary), 'raw', '404']
    with (output / 'extreme-range.json').open('w') as stdout, (output / 'extreme-range.log').open('w') as stderr:
        negative = subprocess.run(argv, stdout=stdout, stderr=stderr, env=env)
    diagnostic = (output / 'extreme-range.log').read_text()
    assert negative.returncode != 0 and 'prepass.h:168' in diagnostic and 'signed integer overflow' in diagnostic
    proof = linked_proof(root / 'normal/frame_calico_check', output)
    assert inputs == {str(p.relative_to(ROOT)): sha(p) for p in paths}, 'Owned inputs changed during qualification'
    verify_archive(archive)
    for profile in ['normal', 'sanitize']:
        verify_sources(root / profile)
    receipt = dict(format=1, scope='Native functional controls only; restricted range endpoints; no timings or worker execution',
        revision=MANIFEST['revision'], source_archive_sha256=sha(archive), source_files=len(MANIFEST['files_sha256']),
        inputs_sha256=inputs, compiler=compiler, host=platform.uname()._asdict(), cpu=args.cpu, jobs=1,
        profiles=profiles, linked_proof=proof,
        nonpassing=[dict(argv=argv, exit=negative.returncode, case=404, fixture='raw_lane256_k63_frames1_patch0',
            diagnostic='prepass.h:168 res_window: signed INT64_MAX - INT64_MIN overflow',
            stderr_sha256=sha(output / 'extreme-range.log'), upstream_unchanged=True)],
        arithmetic='Owner sums exact i128/u128 on bounded logical fixtures; separate leaf modulo2^64 with explicit wide counterexamples',
        range_limit='Positive matrix omits extreme equal endpoints at K>=63 and u64 max lower bound at K64; omissions counted. Original methods have no hidden fallback.',
        readable_window='No extra payload slack requested: separate exact-end protected-page fixtures for symbols/raw body/container body/dense leaf; copied patch allocations have capacity==size. Not a proof for every layout/grain/ISA.',
        timing_claim=False)
    destination = output / 'qualification.json'
    destination.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(receipt=str(destination), results=[p['functional']['witness'] for p in profiles],
        expected_nonpassing_case=404, binary_sha256=profiles[0]['binary_sha256'])))


if __name__ == '__main__':
    main()
