#!/usr/bin/env python3
"""Capture, incrementally build, check and run a selected local comparison."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--select', default=r'^(absent|near|range)/(aos|soa|tag8|tag16|tag32|native|prefix8|prefix16)/q0$')
    parser.add_argument('--rows', type=int, default=65536)
    parser.add_argument('--seed', type=int, default=41)
    parser.add_argument('--hash', type=int, default=13)
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--min-time', type=float, default=.01)
    parser.add_argument('--cpu', type=int, default=0)
    parser.add_argument('--march', default='')
    parser.add_argument('--text', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    # Captured sources must share the checkout's dataset cache, not create a disposable one.
    os.environ.setdefault('SIXDB_DATA_CACHE', str(ROOT / 'build/datasets'))
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('Run on Linux, e.g. orb -m ubuntu python3 ...')
    if args.rows < 1 or args.repetitions < 1 or args.min_time <= 0 or args.cpu not in os.sched_getaffinity(0):
        parser.error('Invalid size, repetition, time or CPU')
    output = ROOT / 'build/experiments/row-filter-signatures' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run = Run(ROOT, output, vars(args) | {'allowed_cpus': sorted(os.sched_getaffinity(0))}, workspace=ROOT / 'build/workspaces/row-filter-signatures')
    build = run.build_dir
    print(f'Run: {output}', flush=True)
    error = None
    try:
        run.step('git-head', ['git', 'rev-parse', 'HEAD'])
        run.step('compiler', ['clang++-21', '--version'])
        run.step('hardware', ['lscpu'])
        source = run.source_root / HERE.relative_to(ROOT)
        run.step('semantics', [sys.executable, str(source / 'semantics.py'), str(output / 'semantics.json')])
        text_path = None
        if args.text:
            run.step('prepare', [sys.executable, str(source / 'prepare.py')])
            text_input = Path((output / 'prepare.stdout').read_text().strip().splitlines()[-1])
            text_path = run.input('text-input', text_input) / 'strings.bin'
        config = ['cmake', '-S', str(run.source_root), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=row-filter-signatures', '-DSIXDB_TUNE=generic', f'-DSIXDB_MARCH={args.march}']
        run.step('configure', config)
        run.step('build', ['cmake', '--build', str(build), '--target', 'row_signatures_check', 'row_signatures_bench', '-j', '4'])
        for name in ('CMakeCache.txt', 'compile_commands.json'):
            shutil.copy2(build / name, output / name)
        for name in ('row_signatures_check', 'row_signatures_bench'):
            shutil.copy2(build / 'workbench/spikes/row-filter-signatures' / name, output / name)
        check_args = [str(text_path)] if text_path else []
        run.step('check', [str(output / 'row_signatures_check'), *check_args])
        if args.sanitize:
            sanitized = build.parent / 'sanitize'
            run.step('configure-sanitize', config[:config.index('-B')+1] + [str(sanitized)] + config[config.index('-B')+2:] + ['-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer', '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g'])
            run.step('build-sanitize', ['cmake', '--build', str(sanitized), '--target', 'row_signatures_check', '-j', '4'])
            shutil.copy2(sanitized / 'workbench/spikes/row-filter-signatures/row_signatures_check', output / 'check_sanitized')
            run.step('check-sanitize', [str(output / 'check_sanitized'), *check_args])
        binary = str(output / 'row_signatures_bench')
        common = [f'--select={args.select}', f'--rows={args.rows}', f'--seed={args.seed}', f'--hash={args.hash}', f'--cpu={args.cpu}']
        if text_path:
            common.append(f'--text={text_path}')
        run.step('accounting', [binary, '--accounting', *common], 'accounting.csv')
        run.step('benchmark', [binary, *common, f'--benchmark_repetitions={args.repetitions}', f'--benchmark_min_time={args.min_time}s', '--benchmark_enable_random_interleaving=false', '--benchmark_report_aggregates_only=false', '--benchmark_display_aggregates_only=true', '--benchmark_color=false', f'--benchmark_out={output / "benchmark.json"}', '--benchmark_out_format=json'])
        run.step('analyze', [sys.executable, str(source / 'analyze.py'), str(output)])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    if error:
        print(f'FAILED: {error}', file=sys.stderr)
        return 1
    print(f'Complete: {output / "summary.md"}')
    print(f'To keep: python3 workbench/tools/artifacts.py retain {output.relative_to(ROOT)} workbench/spikes/row-filter-signatures/evidence/NAME')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
