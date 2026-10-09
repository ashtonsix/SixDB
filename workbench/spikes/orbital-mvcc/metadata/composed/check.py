#!/usr/bin/env python3
"""Captured composed Shape adapter qualification; no benchmark clocks."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import sys

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    run = Run(ROOT, args.output.resolve(), {'scope': 'composed correctness, no timing', 'sanitize': args.sanitize},
              workspace=ROOT / 'build/orbital-mvcc/composed' / ('sanitize' if args.sanitize else 'native'))
    error = None
    try:
        flags = ['-fsanitize=address,undefined', '-fsanitize-address-use-after-return=always', '-fno-omit-frame-pointer'] if args.sanitize else []
        command = ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja',
                   '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=orbital-mvcc', '-DSIXDB_BENCHMARKS=',
                   '-DSIXDB_MARCH=', '-DSIXDB_TUNE=generic']
        if flags:
            command += ['-DCMAKE_CXX_FLAGS=' + ' '.join(flags), '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run.step('configure', command)
        run.step('libraries', ['cmake', '--build', str(run.build_dir), '--target',
            'orbital_versioned_performance_runtime', 'orbital_mvcc_shape', 'orbital_mvcc_directory', '-j2'])
        source = run.source_root
        spike = source / 'workbench/spikes/orbital-mvcc'
        built = run.build_dir / 'workbench/spikes/orbital-mvcc'
        own = run.build_dir / 'composed-check'
        own.mkdir(exist_ok=True)
        compile = ['clang++-21', '-std=c++23', '-O2', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                   '-fno-fast-math', '-ffp-contract=off', '-fdenormal-fp-math=ieee',
                   '-DSIXDB_TUNE_NEOVERSE_V3=0', '-DCONSUMER_METADATA_FROZEN=0', *flags]
        for include in [source / 'orbital/include', source / 'ikea/include', spike / 'baseline/include', spike,
                        source / 'workbench/benchmarks/orbital']:
            compile += ['-I' + str(include)]
        units = [spike / 'metadata/composed/material.cpp', spike / 'metadata/composed/check.cpp',
                 spike / 'metadata/consumer/write_batch.cpp', spike / 'metadata/consumer/shape_read.cpp',
                 spike / 'metadata/consumer/shape_read_bridge.cpp']
        libraries = [built / 'performance/liborbital_versioned_performance_runtime.a',
            built / 'performance/liborbital_versioned_performance_fixture.a', built / 'performance/liborbital_versioned_lmdb.a',
            built / 'metadata/orbital/shape/liborbital_mvcc_shape.a', built / 'metadata/liborbital_mvcc_directory.a',
            built / 'consumer/liborbital_versioned_data.a', built / 'consumer/liborbital_versioned_consumer.a',
            built / 'liborbital_bench_workload.a', built / 'liborbital_mvcc_baseline.a',
            run.build_dir / 'orbital/liborbital.a', run.build_dir / 'ikea/libikea_tuplepack.a',
            run.build_dir / 'ikea/libikea_seriespack.a', run.build_dir / 'ikea/libikea_core.a']
        objects = [own / (unit.stem + '.o') for unit in units]
        binary = own / 'check'
        # Small private Ninja file preserves incremental TU compilation and
        # compiler depfiles. Shared targets remain entirely owned by root CMake.
        lines = ['rule cc', '  command = ' + shlex.join(compile) + ' -MMD -MF $out.d -c $in -o $out',
                 '  depfile = $out.d', '  deps = gcc', 'rule link',
                 '  command = clang++-21 ' + shlex.join(flags) + ' ' + shlex.join(map(str, objects)) +
                 ' -Wl,--start-group ' + shlex.join(map(str, libraries)) + ' -Wl,--end-group -pthread -llmdb -o $out']
        lines += [f'build {obj}: cc {unit}' for obj, unit in zip(objects, units)]
        lines += [f'build {binary}: link ' + ' '.join(map(str, [*objects, *libraries]))]
        (own / 'build.ninja').write_text('\n'.join(lines) + '\n')
        run.step('adapter', ['ninja', '-C', str(own), '-f', 'build.ninja', '-j2', str(binary)])
        run.step('check', ['env', 'ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1:halt_on_error=1',
            'UBSAN_OPTIONS=halt_on_error=1', str(binary)], 'checks.jsonl')
        observations = [json.loads(line) for line in (run.output / 'checks.jsonl').read_text().splitlines()]
        assert observations[-1]['passed']
        receipt = dict(passed=True, source_digest=run.receipt['source_digest'],
            binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), sanitizer_flags=flags, observations=observations,
            scope='real Allocation/Shape/history; independent reads, updates, conversion, retained and late adoption; no timing')
        (run.output / 'qualification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    except Exception as exc:
        error = exc
    finally:
        error = run.finish(error)
    if error:
        raise error
    print(run.output / 'qualification.json')


if __name__ == '__main__':
    main()
