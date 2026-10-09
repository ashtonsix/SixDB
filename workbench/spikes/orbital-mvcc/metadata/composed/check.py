#!/usr/bin/env python3
"""Captured composed Shape adapter qualification; no benchmark clocks."""
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
        run.step('adapter', ['cmake', '--build', str(run.build_dir), '--target',
            'orbital_mvcc_composed_check', '-j2'])
        binary = run.build_dir / 'workbench/spikes/orbital-mvcc/metadata/orbital_mvcc_composed_check'
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
