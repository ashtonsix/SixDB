#!/usr/bin/env python3
"""Build, check, and run raw-string efficacy; does not benchmark FSST or latency."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT/'workbench/tools'))
from experiment import Run

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs', type=Path, help='Override shared cached inputs with an existing prepared directory')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--branch-budget', type=int)
    p.add_argument('--study', choices=('raw', 'factored'), default='raw')
    args = p.parse_args()
    factored = args.study == 'factored'
    if args.branch_budget is None: args.branch_budget = 64 if factored else 8
    if args.branch_budget < 1: p.error('Positive branch budget required')
    if factored and args.branch_budget != 64: p.error('The factored comparison holds the baseline at 64 branches')
    if not sys.platform.startswith('linux'): p.error('Run on Linux, e.g. orb -m ubuntu')
    if args.inputs is None:
        from prepare import prepare
        args.inputs = prepare()
    if not (args.inputs/'inputs.json').exists():
        p.error('Prepare inputs first: python3 workbench/spikes/regexp-lowering/prepare.py build/datasets/regexp-lowering/prepared')
    meta = json.loads((args.inputs/'inputs.json').read_text())
    for name, digest in meta['prepared_sha256'].items():
        with (args.inputs/name).open('rb') as f:
            if hashlib.file_digest(f, 'sha256').hexdigest() != digest: raise ValueError(f'Changed prepared input: {name}')
    output = ROOT/'build/experiments/regexp-lowering'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    build = ROOT/'build/clang/regexp-lowering'
    run = Run(ROOT, output, {'input_manifest': meta, 'sanitize': args.sanitize, 'branch_budget': args.branch_budget, 'study': args.study, 'scope': 'raw-string efficacy, no timing comparison'}, workspace=ROOT / 'build/workspaces/regexp-lowering')
    build = run.build_dir
    drivers = ['check', 'factor_check', 'factor_probe'] if factored else ['check', 'probe']
    executables = ['regexp_lowering_'+name for name in drivers]
    checks = ['check', 'factor_check'] if factored else ['check']
    print(f'Run: {output}', flush=True)
    error = None
    try:
        if (args.inputs / 'prepared.json').exists():
            inputs = run.input('inputs', args.inputs)
        else:
            # Old/custom prepared inputs still work without a dataset recipe.
            shutil.copytree(args.inputs, output/'inputs')
            inputs = output/'inputs'
        shutil.copyfile(args.inputs/'inputs.json', output/'inputs.json')
        run.step('git-head', ['git', 'rev-parse', 'HEAD'])
        run.step('compiler', ['clang++-21', '--version'])
        run.step('hardware', ['lscpu'])
        run.step('configure', ['cmake', '-S', str(run.source_root), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=regexp-lowering', '-DSIXDB_TUNE=generic'])
        run.step('build', ['cmake', '--build', str(build), '--target', *executables, '-j', '4'])
        for name in ('CMakeCache.txt', 'compile_commands.json'): shutil.copy2(build/name, output/name)
        for name in executables:
            shutil.copy2(build/'workbench/spikes/regexp-lowering'/name, output/name)
        for name in checks: run.step(name, [str(output/('regexp_lowering_'+name))])
        if args.sanitize:
            sb = run.build_dir.parent/'sanitize'
            run.step('configure-sanitize', ['cmake', '-S', str(run.source_root), '-B', str(sb), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSIXDB_SPIKES=regexp-lowering', '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer', '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g'])
            run.step('build-sanitize', ['cmake', '--build', str(sb), '--target', *['regexp_lowering_'+name for name in checks], '-j', '4'])
            for name in checks: run.step(name+'-sanitize', [str(sb/'workbench/spikes/regexp-lowering'/('regexp_lowering_'+name))])
        for dataset in ('accidents', 'uap'):
            driver = 'factor_probe' if factored else 'probe'
            command = [str(output/('regexp_lowering_'+driver)), dataset, str(inputs), str(output)]
            if not factored: command.append(str(args.branch_budget))
            run.step(f'probe-{dataset}', command)
            raw = output/(f'{dataset}-factor-outcomes.bin' if factored else f'{dataset}-outcomes.bin')
            with raw.open('rb') as src, raw.with_suffix('.bin.gz').open('wb') as dst:
                with gzip.GzipFile(filename='', mode='wb', fileobj=dst, mtime=0) as compressed: shutil.copyfileobj(src, compressed)
            raw.unlink()
        run.step('analyze', [sys.executable, str(run.source_root / HERE.relative_to(ROOT) / ('factor_analyze.py' if factored else 'analyze.py')), str(output)])
        compact = ['inputs.json', 'summary.csv', 'summary.md', 'examples.csv']
        compact += ['factor_summary.csv', 'structure.csv'] if factored else ['pattern_summary.csv', 'policy_summary.csv']
        run.compact(compact, ['python3', str(HERE.relative_to(ROOT) / ('factor_analyze.py' if factored else 'analyze.py')), '{evidence}'])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    if error:
        print(f'FAILED: {error}', file=sys.stderr); return 1
    print(f'Complete: {output / "summary.md"}')
    print(f'To keep this run: python3 workbench/tools/artifacts.py retain {output.relative_to(ROOT)} workbench/spikes/regexp-lowering/evidence/NAME')
    return 0

if __name__ == '__main__': raise SystemExit(main())
