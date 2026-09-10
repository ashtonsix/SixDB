#!/usr/bin/env python3
"""Captured heterogeneous caller and live provider checks, then sequential measurements."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import sys
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run
import prepare_data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['native', 'zen5', 'granite-rapids', 'neoverse-v2'], default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--synthetic', action='store_true')
    parser.add_argument('bench', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    variant = args.target + ('-sanitize' if args.sanitize else '')
    output = Path(os.environ.get('SIXDB_RESULTS', ROOT / 'build/experiments/ikea-heterogeneous')) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + variant)
    run = Run(ROOT, output, vars(args), workspace=ROOT / 'build/workspaces' / ('ikea-heterogeneous-' + variant))
    error = None
    try:
        data = None if args.synthetic or args.check_only or args.sanitize else run.input('inputs/windows', prepare_data.get())
        flags, cxx = [], []
        if args.target in ['zen5', 'granite-rapids']:
            flags += ['-DSIXDB_MARCH=' + ('znver5' if args.target == 'zen5' else 'graniterapids'), '-DSIXDB_TUNE=' + args.target]
        if args.target == 'neoverse-v2':
            flags += ['-DSIXDB_TUNE=neoverse-v2']; cxx += ['-mcpu=neoverse-v2']
        if args.sanitize:
            cxx += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        if cxx:
            flags += ['-DCMAKE_CXX_FLAGS=' + ' '.join(cxx)]
        run.step('compiler', ['clang++-21', '--version'], 'compiler.txt')
        run.step('cpu', ['lscpu'], 'cpu.txt')
        run.step('cache-context', ['python3', '-c', 'from pathlib import Path; import json; root=Path("/sys/devices/system/cpu/cpu0/cache"); print(json.dumps({str(p):p.read_text().strip() for p in root.rglob("*") if p.is_file() and p.name in {"size","level","type","coherency_line_size","shared_cpu_list"}},indent=2))'], 'cache-context.json')
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=' + ('RelWithDebInfo' if args.sanitize else 'Release'), '-DSIXDB_SPIKES=ikea-blocks;ikea-integers;ikea-heterogeneous'] + flags)
        targets = ['ikea_bitsets_check', 'ikea_integer_check', 'ikea_heterogeneous_check']
        if not args.check_only and not args.sanitize:
            targets.append('ikea_heterogeneous_bench')
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', *targets, '-j', os.environ.get('SIXDB_BUILD_JOBS', '1')])
        base = run.build_dir / 'workbench/spikes'
        compact = ['compiler.txt', 'cpu.txt', 'cache-context.json']
        for name, path in [('bitsets', 'ikea-blocks/ikea_bitsets_check'), ('integers', 'ikea-integers/ikea_integer_check'), ('combined', 'ikea-heterogeneous/ikea_heterogeneous_check')]:
            run.step(name + '-check', [str(base / path)], name + '-checks.txt')
            compact.append(name + '-checks.txt')
        audit = base / 'ikea-heterogeneous' / ('ikea_heterogeneous_check' if len(targets) == 3 else 'ikea_heterogeneous_bench')
        run.step('assembly', ['llvm-objdump-21', '-dr', '-C', str(audit)], 'assembly.txt')
        run.step('symbols', ['llvm-nm-21', '-S', '--size-sort', '-C', str(audit)], 'symbols.txt')
        if len(targets) == 4:
            cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
            run.receipt['pinned_cpu'] = cpu; run.save()
            bench = args.bench[1:] if args.bench[:1] == ['--'] else args.bench
            if data:
                bench = ['--data', str(data), *bench]
            run.step('benchmark', ['taskset', '-c', str(cpu), str(audit), *bench], 'timings.csv')
            run.step('report', ['python3', str(run.source_root / 'workbench/spikes/ikea-heterogeneous/report.py'), str(output)], 'report.txt')
            compact += ['timings.csv', 'summary.csv', 'summary.md']
        shutil.copyfile(audit, output / audit.name)
        run.compact(compact, ['python3', 'workbench/spikes/ikea-heterogeneous/report.py', '{evidence}'] if len(targets) == 4 else [])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    print(output, flush=True)
    if error:
        raise error


if __name__ == '__main__':
    main()
