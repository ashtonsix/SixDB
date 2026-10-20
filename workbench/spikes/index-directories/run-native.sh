#!/bin/bash
# One host, two frozen sources, two ISAs; native driver owns all measured work.
set -euo pipefail
if ! dpkg-query -W liblmdb-dev >/dev/null 2>&1; then
  apt-get -o Acquire::Retries=3 update -qq
  apt-get -o Acquire::Retries=3 install -y -qq liblmdb-dev
fi
python3 - <<'PY'
import hashlib
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

root = Path(os.environ['SIXDB_SOURCE'])
out = Path(os.environ['SIXDB_RESULTS'])
cache = root / 'build/index-directory-native'
cache.mkdir(parents=True, exist_ok=True)
commands = []
def run(argv, log, stderr=None):
    argv = list(map(str, argv))
    commands.append(argv)
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    print(' '.join(argv), flush=True)
    with (out / log).open('w') as output:
        if stderr:
            with (out / stderr).open('w') as errors:
                subprocess.run(argv, stdout=output, stderr=errors, check=True)
        else:
            subprocess.run(argv, stdout=output, stderr=subprocess.STDOUT, check=True)
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def core(cpu):
    topology = Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
    return tuple((topology / name).read_text().strip()
                 for name in ('physical_package_id', 'core_id'))
allowed = sorted(os.sched_getaffinity(0))
owner = int(os.environ['SIXDB_CPU'])
assert len(allowed) >= 4 and owner in allowed, ('need >=4 allowed CPUs', allowed, owner)
helper = next(cpu for cpu in reversed(allowed) if core(cpu) != core(owner))
flags = next(line.split(':', 1)[1].split() for line in
             Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('flags'))
assert {'avx2', 'avx512f', 'avx512dq', 'avx512bw', 'avx512vl'} <= set(flags)
run(['lscpu', '--json'], 'lscpu.json')
run(['clang++-21', '--version'], 'compiler.txt')
run(['g++', '--version'], 'stdlib-toolchain.txt')
run(['dpkg-query', '-W', 'liblmdb-dev', 'liblmdb0', 'libstdc++6'], 'packages.txt')
receipt = {'owner_cpu': owner, 'helper_cpu': helper, 'allowed_cpus': allowed,
           'owner_core': core(owner), 'helper_core': core(helper), 'sources': {},
           'scope': '8K synchronous projected/material reads and moving-Part writes; '
                    'foreground plus declared maintenance, not cold/p99/durability/e2e'}
sources = {}
for label in ('base', 'hot'):
    uri = os.environ[f'INDEX_DIRECTORY_{label.upper()}_ARCHIVE']
    expected = os.environ[f'INDEX_DIRECTORY_{label.upper()}_SHA256']
    archive = cache / f'{expected}.tar.gz'
    run(['aws', 's3', 'cp', uri, archive, '--only-show-errors'], f'{label}-download.txt')
    assert sha(archive) == expected, f'{label} source archive hash changed'
    source = cache / f'source-{expected}'
    if source.exists():
        shutil.rmtree(source)
    source.mkdir()
    with tarfile.open(archive, 'r:gz') as tar:
        tar.extractall(source, filter='data')
    sources[label] = source
    receipt['sources'][label] = {'uri': uri, 'archive_sha256': expected,
                                'driver_sha256': sha(source / 'workbench/spikes/index-directories/src/driver.cpp')}
assert receipt['sources']['base']['driver_sha256'] == receipt['sources']['hot']['driver_sha256']
(out / 'placement-and-inputs.json').write_text(json.dumps(receipt, indent=2) + '\n')
executables = {}
binary_hashes = {}
for label, isa in itertools.product(('base', 'hot'), ('avx2', 'avx512')):
    name = f'{label}-{isa}'
    build = cache / f'build-{sources[label].name}-{isa}'
    march = 'x86-64-v3' if isa == 'avx2' else 'x86-64-v4'
    run(['cmake', '-S', sources[label], '-B', build, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
         '-DSIXDB_SPIKES=index-directories', f'-DSIXDB_MARCH={march}',
         '-DSIXDB_TUNE=zen5'], f'{name}-configure.txt')
    run(['cmake', '--build', build, '--target', 'index_directory_check',
         'index_directory_material_check', 'index_directory_driver',
         '--parallel', os.environ['SIXDB_BUILD_JOBS']], f'{name}-build.txt')
    binary = build / 'workbench/spikes/index-directories'
    for check in ('index_directory_check', 'index_directory_material_check'):
        run(['taskset', '-c', owner, binary / check], f'{name}-{check}.txt')
    executable = binary / 'index_directory_driver'
    executables[label, isa] = executable
    binary_hashes[f'{label}-{isa}'] = sha(executable)
    shutil.copy2(build / 'compile_commands.json', out / f'{name}-compile-commands.json')
    shutil.copy2(build / 'CMakeCache.txt', out / f'{name}-CMakeCache.txt')
    shutil.copy2(executable, out / f'{name}-driver.bin')
    run(['ldd', executable], f'{name}-linked-libraries.txt')
    run(['llvm-objdump-21', '-dr', '--demangle', '--no-show-raw-insn', executable],
        f'{name}-disassembly.txt')
    run(['clang++-21', f'-march={march}', '-mtune=znver5', '-dM', '-E', '-x', 'c++', '/dev/null'],
        f'{name}-compiler-macros.txt')
cells = list(itertools.product(('base', 'hot'), ('avx2', 'avx512'),
                               ('dense', 'sparse'), ('metadata-first', 'material-first')))
for trial in range(3):
    for label, isa, occupancy, layout in (cells if trial % 2 == 0 else reversed(cells)):
        name = f't{trial}-{label}-{isa}-{occupancy}-{layout}'
        database = cache / f'db-{os.environ["SIXDB_JOB"]}-{name}'
        database.mkdir(exist_ok=False)
        run(['taskset', '-c', owner, executables[label, isa], database, 8192, 262144,
             4096, occupancy, layout, helper], f'{name}.csv', f'{name}.stderr.txt')
        shutil.rmtree(database)
assert binary_hashes == {f'{label}-{isa}': sha(path) for (label, isa), path in executables.items()}
(out / 'binary-sha256.json').write_text(json.dumps(binary_hashes, indent=2) + '\n')
PY
