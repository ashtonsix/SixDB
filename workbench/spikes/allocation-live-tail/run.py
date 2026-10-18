#!/usr/bin/env python3
"""Freeze paired allocator sources, measure fresh processes, retain compact receipts."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
STUDY = Path('workbench/spikes/allocation-live-tail')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log, cwd=ROOT):
    with log.open('w') as stream:
        subprocess.run(list(map(str, command)), cwd=cwd, check=True,
                       stdout=stream, stderr=subprocess.STDOUT)


def write_csv(path, rows):
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--reclaimer-cpu', type=int)
    parser.add_argument('--trials', type=int, default=6)
    parser.add_argument('--loops', type=int, default=1048576)
    parser.add_argument('--batch', type=int, default=128)
    parser.add_argument('--march', default='native')
    parser.add_argument('--tune', default='generic')
    parser.add_argument('--candidate-only', action='store_true', help='diagnostic; skips paired baseline')
    args = parser.parse_args()
    allowed = sorted(os.sched_getaffinity(0))
    if args.reclaimer_cpu is None:
        args.reclaimer_cpu = next((cpu for cpu in allowed if cpu != args.cpu), args.cpu)
    if args.cpu not in allowed or args.reclaimer_cpu not in allowed or min(args.trials, args.loops, args.batch) < 1:
        parser.error('CPUs must be allowed and counts positive')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    capture = ROOT / 'workbench/tools/capture.py'
    patch = ROOT / STUDY / 'baseline.patch'
    spec = None if args.candidate_only else json.loads((ROOT / STUDY / 'baseline.json').read_text())
    if spec and digest(patch) != spec['patch_sha256']:
        raise ValueError('baseline.patch does not match baseline.json')
    sources = {'candidate': out / 'source-candidate'}
    command = [sys.executable, capture, 'create', sources['candidate']]
    for name, hashes in (spec or {}).get('files', {}).items():
        command += ['--expect', name + '=' + hashes['candidate']]
    run(command, out / 'candidate-capture.log')
    if spec:
        inputs = out / 'baseline-inputs'
        for name in spec['files']:
            path = inputs / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sources['candidate'] / name, path)
        apply = ['git', 'apply', '--unsafe-paths', '--directory=' + str(inputs), patch]
        if spec.get('direction') == 'reverse':
            apply.insert(2, '--reverse')
        run(apply[:2] + ['--check'] + apply[2:], out / 'patch-check.log')
        run(apply, out / 'patch-apply.log')
        sources['baseline'] = out / 'source-baseline'
        command = [sys.executable, capture, 'create', sources['baseline'], '--base', sources['candidate']]
        for name, hashes in spec['files'].items():
            if digest(inputs / name) != hashes['baseline']:
                raise ValueError('reconstructed baseline mismatch: ' + name)
            command += ['--replace', name + '=' + str(inputs / name), '--expect', name + '=' + hashes['baseline']]
        run(command, out / 'baseline-capture.log')
    binaries = {}
    clang_tune = {'zen5': 'znver5', 'granite-rapids': 'graniterapids'}.get(args.tune, args.tune)
    for name, source in sources.items():
        build = out / ('build-' + name)
        run(['cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DSIXDB_MARCH=' + args.march, '-DSIXDB_TUNE=' + args.tune], out / (name + '-configure.log'))
        targets = ['orbital', 'orbital_live_tail_check'] if name == 'candidate' else ['orbital']
        run(['cmake', '--build', build, '--target', *targets, '-j', os.environ.get('SIXDB_BUILD_JOBS', '2')],
            out / (name + '-build.log'))
        if name == 'candidate':
            run(['taskset', '-c', args.cpu, build / 'orbital/orbital_live_tail_check'], out / 'candidate-check.log')
        binaries[name] = out / ('bench-' + name)
        run(['clang++-21', '-std=c++23', '-O3', '-g', '-Wall', '-Wextra', '-Wpedantic',
             '-fno-fast-math', '-ffp-contract=off', '-fdenormal-fp-math=ieee',
             '-march=' + args.march, '-mtune=' + clang_tune,
             '-DSIXDB_LIVE_TAIL_API=' + str(int(name == 'candidate')),
             '-I' + str(source / 'orbital/include'), '-I' + str(source / 'orbital/src/allocation'),
             sources['candidate'] / STUDY / 'bench.cpp', build / 'orbital/liborbital.a',
             '-pthread', '-o', binaries[name]], out / (name + '-bench-build.log'))
    rows, executions = [], []
    for trial in range(args.trials):
        order = list(sources) if trial % 2 else list(reversed(sources))
        cases = [(name, 'ordinary') for name in order]
        feature_order = ['retain', 'reclaim'] if trial % 2 else ['reclaim', 'retain']
        cases += [('candidate', mode) for mode in feature_order + ['continuous']]
        for name, mode in cases:
            path = out / f'{trial}-{name}-{mode}.csv'
            command = ['taskset', '-c', args.cpu, binaries[name], mode, args.loops, args.batch, args.reclaimer_cpu]
            run(command, path)
            samples = list(csv.DictReader(path.open()))
            rows += [dict(variant=name, trial=trial, mode=mode, **row) for row in samples]
            executions.append(dict(trial=trial, variant=name, mode=mode, csv=path.name))
            print('measured', trial, name, mode, flush=True)
    write_csv(out / 'samples.csv', rows)
    summary = []
    for case in dict.fromkeys(row['case'] for row in rows if row['mode'] == 'ordinary'):
        paired = {name: [float(row['ns_per_cycle']) for row in rows if row['variant'] == name and row['case'] == case]
                  for name in sources}
        summary.append(dict(case=case, candidate_median_ns=statistics.median(paired['candidate']),
            baseline_median_ns=statistics.median(paired['baseline']) if spec else '',
            paired_change_percent=statistics.median(100 * (c / b - 1) for b, c in zip(paired['baseline'], paired['candidate']))
                if spec and all(paired['baseline']) else ''))
    write_csv(out / 'summary.csv', summary)
    source_digests = {}
    for name, source in sources.items():
        run([sys.executable, capture, 'verify', source], out / (name + '-verify.log'))
        source_digests[name] = json.loads(source.with_name(source.name + '.capture.json').read_text())['source_digest']
    run(['clang++-21', '--version'], out / 'compiler.txt')
    run(['lscpu', '--json'], out / 'lscpu.json')
    context = dict(settings=vars(args), host=platform.uname()._asdict(), executions=executions,
        source_digests=source_digests, binary_sha256={name: digest(path) for name, path in binaries.items()},
        reverse_patch_sha256=digest(patch) if spec else None,
        harness_sha256=digest(sources['candidate'] / STUDY / 'bench.cpp'),
        scope='Fresh processes, alternating ordinary baseline/candidate; whole-loop wall means, not individual percentiles. '
              'Feature cases use actual Orbital. Model rows count pages only; metadata census excludes general controller heap. '
              'Resident pages use mincore without reading data; charge/accessibility/VA are distinct.',
        qualification='All timed processes checked retained prefix, stable regrowth address, tail writes and zero charge on close.')
    (out / 'context.json').write_text(json.dumps(context, indent=2, default=str) + '\n')
    print(out, flush=True)


if __name__ == '__main__':
    main()
