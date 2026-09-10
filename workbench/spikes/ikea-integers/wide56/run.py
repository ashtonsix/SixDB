#!/usr/bin/env python3
"""Captured width-56 checks and matched point/group/bulk comparisons."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from experiment import Run
import prepare_prior


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['native', 'zen5', 'granite-rapids', 'neoverse-v2', 'avx2-qemu'], default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--encode-region32', action='store_true')
    parser.add_argument('bench', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.encode_region32 and args.target not in ['zen5', 'granite-rapids']:
        parser.error('--encode-region32 requires an AVX-512 BW+VBMI target')
    variant = args.target + ('-sanitize' if args.sanitize else '')
    if args.encode_region32:
        variant += '-encode32'
    output = Path(os.environ.get('SIXDB_RESULTS', ROOT / 'build/experiments/ikea-integers-wide56')) / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + variant)
    run = Run(ROOT, output, vars(args), workspace=ROOT / 'build/workspaces' / ('ikea-integers-wide56-' + variant))
    error = None
    try:
        prior = run.input('inputs/prior', prepare_prior.get())
        cmake, cxx = [], []
        if args.encode_region32:
            cxx += ['-DW56_ENCODE_REGION32=1']
        if args.target in ['zen5', 'granite-rapids']:
            cmake += ['-DSIXDB_MARCH=' + ('znver5' if args.target == 'zen5' else 'graniterapids'), '-DSIXDB_TUNE=' + args.target]
        if args.target == 'neoverse-v2':
            cxx += ['-mcpu=neoverse-v2']
            cmake += ['-DSIXDB_TUNE=neoverse-v2']
        if args.target == 'avx2-qemu':
            cxx += ['--target=x86_64-linux-gnu', '-march=x86-64-v3']
            cmake += ['-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_SYSTEM_PROCESSOR=x86_64']
        if args.sanitize:
            cxx += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        if cxx:
            cmake += ['-DCMAKE_CXX_FLAGS=' + ' '.join(cxx)]
        run.step('compiler', ['clang++-21', '--version'], 'compiler.txt')
        run.step('cpu', ['lscpu'], 'cpu.txt')
        run.step('cache-context', ['python3', '-c', 'from pathlib import Path; import json; roots=[Path("/sys/devices/system/cpu/cpu0/cache"),Path("/sys/kernel/mm/transparent_hugepage")]; print(json.dumps({str(p):p.read_text().strip() for root in roots for p in root.rglob("*") if p.is_file() and p.name in {"size","level","type","coherency_line_size","shared_cpu_list","enabled","defrag"}},indent=2))'], 'cache-context.json')
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(run.build_dir), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=' + ('RelWithDebInfo' if args.sanitize else 'Release'), '-DSIXDB_SPIKES=ikea-integers', '-DIKEA_INTEGER_PRIOR_DIR=' + str(prior)] + cmake)
        names = ['ikea_integer_wide56_check', 'ikea_integer_wide56_bench']
        run.step('build', ['cmake', '--build', str(run.build_dir), '--target', *names, '-j', os.environ.get('SIXDB_BUILD_JOBS', '1')])
        binaries = run.build_dir / 'workbench/spikes/ikea-integers/wide56'
        prefix = ['qemu-x86_64', '-cpu', 'max', '-L', '/usr/x86_64-linux-gnu'] if args.target == 'avx2-qemu' else []
        run.step('check', prefix + [str(binaries / names[0])], 'checks.txt')
        run.step('comparator-check', prefix + [str(binaries / names[1]), '--check-only'], 'comparators.txt')
        run.step('assembly', ['llvm-objdump-21', '-dr', '-C', str(binaries / names[1])], 'assembly.txt')
        compact = ['checks.txt', 'comparators.txt', 'compiler.txt', 'cpu.txt', 'cache-context.json']
        if not args.check_only and not args.sanitize and args.target != 'avx2-qemu':
            cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
            run.receipt['pinned_cpu'] = cpu
            run.save()
            bench = args.bench[1:] if args.bench[:1] == ['--'] else args.bench
            run.step('benchmark', ['taskset', '-c', str(cpu), str(binaries / names[1]), *bench], 'timings.csv')
            run.step('report', ['python3', str(run.source_root / 'workbench/spikes/ikea-integers/wide56/report.py'), str(output)], 'report.txt')
            compact += ['timings.csv', 'summary.csv', 'summary.md']
        for name in names:
            shutil.copyfile(binaries / name, output / name)
        run.compact(compact, ['python3', 'workbench/spikes/ikea-integers/wide56/report.py', '{evidence}'] if 'timings.csv' in compact else [])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    print(output, flush=True)
    if error:
        raise error


if __name__ == '__main__':
    main()
