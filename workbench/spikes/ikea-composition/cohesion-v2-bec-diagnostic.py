#!/usr/bin/env python3
"""Repeat the frozen Bec consumer with fixed iterations and layout controls.

No timed source is patched. The ordinary frozen Google Benchmark callbacks,
fixtures, body oracles and kernels are rebuilt verbatim. This is a diagnostic
for observed timing differences despite equal hot code, not a replacement for
the original five-trial paired cohort. perf and ASLR controls are best effort;
their availability and failures are retained.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

SPEC = importlib.util.spec_from_file_location('cohesion_native', Path(__file__).with_name('cohesion-v2-native.py'))
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)
ROOT = native.ROOT
CASES = ['codec/random/encode_checked_exact', 'codec/random/prepare_write',
         'codec/terminal/read_exact', 'codec/terminal/read_padded']


def main():
    out = Path(os.environ['SIXDB_RESULTS']).resolve()
    inputs = ROOT / native.INPUTS
    manifest = json.loads((inputs / 'manifest.json').read_text())
    for arm, files in manifest['source_sha256'].items():
        for name, digest in files.items():
            assert native.sha((inputs / arm / name).read_bytes()) == digest, (arm, name)
    shutil.copy2(inputs / 'manifest.json', out / 'source-manifest.json')
    cpu = int(os.environ['SIXDB_CPU'])
    receipt = {'source_manifest_sha256': manifest['source_manifest_sha256'],
               'runner_sha256': native.sha(Path(__file__).read_bytes()),
               'scope': __doc__, 'cpu': cpu, 'trials': 5, 'iterations': 5000000,
               'cases': CASES, 'commands': [], 'binaries': []}
    native.ACTIVE_RECEIPT, native.RECEIPT_PATH = receipt, out / 'receipt.json'

    def command(argv, log, cwd=ROOT):
        native.run_command(argv, cwd=cwd, log=log, receipt=receipt['commands'])

    # Keep any unavailable control explicit. No sysctl or machine policy change.
    probes = {}
    for name, argv in [('fixed_aslr', ['setarch', os.uname().machine, '-R', 'true']),
                       ('perf', ['perf', 'stat', '-e', 'cycles:u,instructions:u', '--', 'true']),
                       ('sudo_perf', ['sudo', '-n', 'perf', 'stat', '-e', 'cycles:u,instructions:u', '--', 'true'])]:
        try:
            result = subprocess.run(argv, capture_output=True, text=True)
            probes[name] = {'argv': argv, 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
        except OSError as error:
            probes[name] = {'argv': argv, 'returncode': None, 'error': str(error)}
    receipt['controls'] = probes
    perf = ['perf'] if probes['perf']['returncode'] == 0 else ['sudo', '-n', 'perf'] if probes['sudo_perf']['returncode'] == 0 else []
    modes = ['default_aslr'] + (['fixed_aslr'] if probes['fixed_aslr']['returncode'] == 0 else [])
    receipt['modes'] = modes
    receipt['perf_command'] = perf
    profiles = ['neon'] if os.uname().machine == 'aarch64' else ['avx2', 'avx512']
    receipt['profiles'] = profiles
    for profile in profiles:
        march, tune, _ = native.PROFILES[profile]
        binaries = {}
        for arm in ['before', 'after']:
            source, logs = inputs / arm, out / profile / arm
            logs.mkdir(parents=True, exist_ok=True)
            build = ROOT / 'build/ikea-cohesion-v2-bec-diagnostic' / profile / arm
            command(['cmake', '-S', str(ROOT / native.STUDY / 'cohesion-native'), '-B', str(build), '-G', 'Ninja',
                     '-DCOHESION_SOURCE_ROOT=' + str(source), '-DCOHESION_BEFORE=' + str(int(arm == 'before')),
                     '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_BENCHMARKS=bec', '-DIKEA_COMPARISONS=OFF',
                     '-DSIXDB_MARCH=' + march, '-DSIXDB_TUNE=' + tune], logs / 'configure.log')
            command(['cmake', '--build', str(build), '--target', 'ikea_bec_bench', '-j', os.environ.get('SIXDB_BUILD_JOBS', '2')], logs / 'build.log')
            binary = logs / 'ikea_bec_bench'
            shutil.copy2(build / 'sixdb/workbench/benchmarks/bec/ikea_bec_bench', binary)
            binaries[arm] = binary
            shutil.copy2(build / 'compile_commands.json', logs / 'compile_commands.json')
            receipt['binaries'].append({'profile': profile, 'arm': arm, 'sha256': native.sha(binary.read_bytes())})
            command(['llvm-objdump-21', '-d', '-r', '--demangle', str(binary)], logs / 'disassembly.txt')
        for mode in modes:
            for trial in range(5):
                for arm in (['before', 'after'] if trial % 2 == 0 else ['after', 'before']):
                    logs = out / profile / arm
                    for case in CASES:
                        slug = case.replace('/', '-') + f'-{mode}-{trial}'
                        target = ['bash', '-c', 'exec -a bec "$@"', 'cohesion-fixed-argv', str(binaries[arm]),
                                  '--benchmark_filter=^' + case + '$', '--benchmark_min_time=5000000x',
                                  '--benchmark_repetitions=1', '--benchmark_out_format=json',
                                  '--benchmark_out=' + str(logs / (slug + '.json'))]
                        if mode == 'fixed_aslr':
                            target = ['setarch', os.uname().machine, '-R', *target]
                        if perf:
                            target = [*perf, 'stat', '-x', ',', '-o', str(logs / (slug + '-perf.csv')),
                                      '-e', 'cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u', '--', *target]
                        command(['taskset', '-c', str(cpu), *target], logs / (slug + '.log'), inputs / arm)
    native.json_file(out / 'receipt.json', receipt)


if __name__ == '__main__':
    main()
