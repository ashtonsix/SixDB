#!/usr/bin/env python3
"""Capture whole-window analysis or masked algebra, with live provider checks."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import sys
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run
import datasets
import prepare_data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['native', 'zen5', 'granite-rapids', 'neoverse-v2'], default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--quality', action='store_true')
    parser.add_argument('--synthetic', action='store_true')
    parser.add_argument('--part', choices=['algebra', 'analyser', 'both'], default='both')
    parser.add_argument('--grain', choices=[1, 2], type=int, default=2)
    parser.add_argument('bench', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.quality and (args.sanitize or args.check_only or args.synthetic):
        parser.error('--quality is a separate complete-input run')
    variant = args.target + ('-quality' if args.quality else '-sanitize' if args.sanitize else '')
    if args.grain != 2:
        variant += '-grain' + str(args.grain)
    output = Path(os.environ.get('SIXDB_RESULTS', ROOT / 'build/experiments/ikea-heterogeneous-operations')) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + variant)
    run = Run(ROOT, output, vars(args), workspace=ROOT / 'build/workspaces' / ('ikea-heterogeneous-operations-' + variant))
    error = None
    try:
        pairs = None
        if args.quality:
            roaring = run.input('inputs/real-roaring', datasets.get('real-roaring'))
            msmarco = run.input('inputs/msmarco-keyset', datasets.get('msmarco-keyset'))
            # Run.input records external shared objects; retain only their small
            # verified manifests here so the report can check the full census.
            for name, source in [('real-roaring', roaring), ('msmarco-keyset', msmarco)]:
                shutil.copyfile(source / 'prepared.json', output / (name + '-prepared.json'))
        elif not (args.synthetic or args.sanitize or args.check_only):
            pairs = run.input('inputs/pairs', prepare_data.get())
        flags, cxx = ['-DSIXDB_HETEROGENEOUS_OUTPUT_GRAIN=' + str(args.grain)], []
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
        targets = ['ikea_bitsets_check', 'ikea_integer_check', 'ikea_heterogeneous_check', 'ikea_heterogeneous_operations_check']
        measure = not (args.sanitize or args.check_only)
        if measure:
            targets.append('ikea_heterogeneous_quality' if args.quality else 'ikea_heterogeneous_operations_bench')
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', *targets, '-j', os.environ.get('SIXDB_BUILD_JOBS', '1')])
        base = run.build_dir / 'workbench/spikes'
        compact = ['compiler.txt', 'cpu.txt', 'cache-context.json']
        for name, path in [('bitsets', 'ikea-blocks/ikea_bitsets_check'), ('integers', 'ikea-integers/ikea_integer_check'), ('range', 'ikea-heterogeneous/ikea_heterogeneous_check'), ('operations', 'ikea-heterogeneous/ikea_heterogeneous_operations_check')]:
            run.step(name + '-check', [str(base / path)], name + '-checks.txt')
            compact.append(name + '-checks.txt')
        audit = base / 'ikea-heterogeneous' / (targets[-1] if measure else 'ikea_heterogeneous_operations_check')
        if args.quality:
            run.step('quality', [str(audit), '--roaring', str(roaring), '--msmarco', str(msmarco)], 'quality.csv')
            run.step('quality-report', ['python3', str(run.source_root / 'workbench/spikes/ikea-heterogeneous/operations/quality_report.py'), str(output)], 'quality-report.txt')
            compact += ['quality-summary.csv', 'quality-summary.md', 'real-roaring-prepared.json', 'msmarco-keyset-prepared.json']
            regenerate = []  # Whole-window raw census remains in the full bundle.
        elif measure:
            cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
            run.receipt['pinned_cpu'] = cpu; run.save()
            bench = args.bench[1:] if args.bench[:1] == ['--'] else args.bench
            if pairs:
                bench = ['--data', str(pairs), *bench]
            for part in (['algebra', 'analyser'] if args.part == 'both' else [args.part]):
                run.step(part, ['taskset', '-c', str(cpu), str(audit), '--part', part, *bench], part + '-timings.csv')
            run.step('report', ['python3', str(run.source_root / 'workbench/spikes/ikea-heterogeneous/operations/report.py'), str(output)], 'timing-report.txt')
            if args.part in ['both', 'algebra']:
                compact += ['algebra-selected.csv', 'algebra-summary.csv', 'algebra-summary.md']
            if args.part in ['both', 'analyser']:
                compact += ['analyser-timings.csv', 'analyser-summary.csv', 'analyser-summary.md']
            regenerate = ['python3', 'workbench/spikes/ikea-heterogeneous/operations/report.py', '{evidence}']
        else:
            regenerate = []
        run.step('assembly', ['llvm-objdump-21', '-dr', '-C', str(audit)], 'assembly.txt')
        run.step('symbols', ['llvm-nm-21', '-S', '--size-sort', '-C', str(audit)], 'symbols.txt')
        shutil.copyfile(audit, output / audit.name)
        run.compact(compact, regenerate)
    except Exception as exc:
        error = exc
    error = run.finish(error)
    print(output, flush=True)
    if error:
        raise error


if __name__ == '__main__':
    main()
