#!/usr/bin/env python3
"""Build, verify, and measure the trie-remapping probe on Linux."""
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
    parser.add_argument('--select', default=r'^(d05|spread)/(keys|deps)/[^/]+/(points|churn)$')
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--min-time', type=float, default=.02)
    parser.add_argument('--cpu', type=int, default=0)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--natural-slack', type=int, choices=range(0,101), default=25, metavar='PERCENT')
    args = parser.parse_args()
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('Run on Linux, e.g. with orb -m ubuntu')
    if args.repetitions < 1 or args.min_time <= 0 or args.cpu not in os.sched_getaffinity(0):
        parser.error('Invalid repetitions, minimum time, or CPU')
    output = ROOT / 'build/experiments/trie-remapping' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    build = ROOT / 'build/clang/trie-remapping'
    run = Run(ROOT, output, vars(args) | {'allowed_cpus': sorted(os.sched_getaffinity(0))})
    print(f'Run: {output}', flush=True)
    error = None
    try:
        run.step('git-head', ['git', 'rev-parse', 'HEAD'])
        run.step('git-status', ['git', 'status', '--short'])
        run.step('compiler', ['clang++-21', '--version'])
        run.step('hardware', ['lscpu'])
        run.step('dev-configure', [sys.executable, str(ROOT / 'workbench/tools/dev.py'), '--add', 'trie-remapping'])
        run.step('configure', ['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=trie-remapping', '-DSIXDB_TUNE=generic'])
        run.step('build', ['cmake', '--build', str(build), '--target', 'trie_remapping_check', 'trie_remapping_bench', '-j', '4'])
        for name in ('CMakeCache.txt', 'compile_commands.json'):
            shutil.copy2(build / name, output / name)
        for name in ('trie_remapping_check', 'trie_remapping_bench'):
            shutil.copy2(build / 'workbench/spikes/trie-remapping' / name, output / name)
        run.step('check', [str(output / 'trie_remapping_check')])
        if args.sanitize:
            sanitized = ROOT / 'build/clang/trie-remapping-sanitize'
            run.step('configure-sanitize', ['cmake', '-S', str(ROOT), '-B', str(sanitized), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=trie-remapping', '-DSIXDB_TUNE=generic', '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer', '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g'])
            run.step('build-sanitize', ['cmake', '--build', str(sanitized), '--target', 'trie_remapping_check', '-j', '4'])
            shutil.copy2(sanitized / 'workbench/spikes/trie-remapping/trie_remapping_check', output / 'trie_remapping_check_sanitized')
            run.step('check-sanitize', [str(output / 'trie_remapping_check_sanitized')])
        binary = str(output / 'trie_remapping_bench')
        common = [f'--select={args.select}', f'--cpu={args.cpu}', f'--natural-slack={args.natural_slack}']
        run.step('accounting', [binary, '--accounting', *common], 'accounting.csv')
        run.step('benchmark', [binary, *common, f'--benchmark_repetitions={args.repetitions}', f'--benchmark_min_time={args.min_time}s', '--benchmark_enable_random_interleaving=false', '--benchmark_report_aggregates_only=false', '--benchmark_display_aggregates_only=true', '--benchmark_color=false', f'--benchmark_out={output / "benchmark.json"}', '--benchmark_out_format=json'])
        run.step('analyse', [sys.executable, str(HERE / 'analyze.py'), str(output)])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    if error:
        print(f'FAILED: {error}', file=sys.stderr)
        return 1
    print(f'Complete: {output / "summary.md"}')
    print(f'To keep this run: python3 workbench/tools/artifacts.py retain {output.relative_to(ROOT)} workbench/spikes/trie-remapping/evidence/NAME')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
