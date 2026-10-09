#!/usr/bin/env python3
"""Freeze and qualify the actual compact read adapters; no performance timing."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cpu', type=int, default=2)
    args = parser.parse_args()
    suffix = 'sanitize' if args.sanitize else 'native'
    run = Run(ROOT, args.output.resolve(), {'scope':'actual adapter qualification; no timing',
        'sanitize':args.sanitize, 'cpu':args.cpu},
        workspace=ROOT / 'build/orbital-mvcc/metadata-adapter' / suffix)
    error = None
    try:
        run.step('compiler', ['clang++-21', '--version'])
        flags = ['-fsanitize=address,undefined', '-fsanitize-address-use-after-return=always',
                 '-fno-omit-frame-pointer'] if args.sanitize else []
        configure = ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=orbital-mvcc', '-DSIXDB_BENCHMARKS=',
            '-DSIXDB_MARCH=', '-DSIXDB_TUNE=generic']
        if flags:
            configure += ['-DCMAKE_CXX_FLAGS='+' '.join(flags),
                          '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run.step('configure', configure)
        run.step('build-libraries', ['taskset', '-c', str(args.cpu), 'cmake', '--build',
            str(run.build_dir), '--target', 'orbital_versioned_performance_runtime', '-j1'])
        source = run.source_root
        spike = source / 'workbench/spikes/orbital-mvcc'
        build = run.build_dir
        built_spike = build / spike.relative_to(source)
        binary = run.output / 'check'
        command = ['taskset', '-c', str(args.cpu), 'clang++-21', '-std=c++23', '-O2', '-g',
            '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-fast-math', '-ffp-contract=off',
            '-fdenormal-fp-math=ieee', '-DSIXDB_TUNE_NEOVERSE_V3=0', *flags]
        for include in [source/'orbital/include', source/'ikea/include', spike/'baseline/include',
                        spike, source/'workbench/benchmarks/orbital']:
            command += ['-I'+str(include)]
        command += [str(spike/'metadata/performance_adapter/check.cpp'), '-Wl,--start-group',
            str(built_spike/'performance/liborbital_versioned_performance_runtime.a'),
            str(built_spike/'performance/liborbital_versioned_performance_fixture.a'),
            str(built_spike/'performance/liborbital_versioned_lmdb.a'),
            str(built_spike/'consumer/liborbital_versioned_data.a'),
            str(built_spike/'consumer/liborbital_versioned_consumer.a'),
            str(built_spike/'liborbital_bench_workload.a'),
            str(built_spike/'liborbital_mvcc_baseline.a'),
            str(build/'orbital/liborbital.a'), str(build/'ikea/libikea_tuplepack.a'),
            str(build/'ikea/libikea_seriespack.a'), str(build/'ikea/libikea_core.a'),
            '-Wl,--end-group', '-pthread', '-llmdb', '-o', str(binary)]
        run.step('build-check', command)
        run.step('check', ['env', 'ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1:halt_on_error=1',
            'UBSAN_OPTIONS=halt_on_error=1', 'taskset', '-c', str(args.cpu), str(binary)], 'qualification.json')
        run.receipt['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        run.receipt['qualification'] = json.loads((run.output/'qualification.json').read_text())
    except Exception as exc:
        error = exc
    finally:
        error = run.finish(error)
    if error:
        raise error
    print(run.output/'qualification.json')


if __name__ == '__main__':
    main()
