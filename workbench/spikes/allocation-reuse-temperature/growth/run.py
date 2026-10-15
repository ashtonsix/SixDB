#!/usr/bin/env python3
"""Compare separate captured allocators, reversing the indexed selector for baseline."""
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

sys.dont_write_bytecode = True
from instrument import write_counter

ROOT = Path(__file__).resolve().parents[4]
STUDY = Path('workbench/spikes/allocation-reuse-temperature/growth')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log, cwd=None):
    with log.open('w') as stream:
        subprocess.run([str(x) for x in command], cwd=cwd, check=True,
                       stdout=stream, stderr=subprocess.STDOUT)


def execute(binary, options, path):
    with path.open('w') as stream, path.with_suffix('.log').open('w') as errors:
        subprocess.run([str(binary), *map(str, options)], check=True, stdout=stream, stderr=errors)
    return list(csv.DictReader(path.open()))


def write_csv(path, rows):
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize(rows):
    keys = ('case', 'n', 'from_bytes', 'to_bytes', 'reservation', 'operations')
    groups = {}
    for row in rows:
        group = groups.setdefault(tuple(row[k] for k in keys), {})
        group.setdefault(row['variant'], {})[int(row['trial'])] = float(row['ns_per_op'])
    output = []
    for key, versions in groups.items():
        baseline, candidate = versions['baseline'], versions['candidate']
        output.append(dict(zip(keys, key),
            baseline_median_ns=statistics.median(baseline.values()),
            candidate_median_ns=statistics.median(candidate.values()),
            paired_change_percent=statistics.median(
                100 * (candidate[t] / baseline[t] - 1) for t in baseline)))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--reclaimer-cpu', type=int)
    parser.add_argument('--trials', type=int, default=6)
    parser.add_argument('--operations', type=int, default=1048576)
    parser.add_argument('--march', default='native')
    parser.add_argument('--tune', default='generic')
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--checks-only', action='store_true')
    args = parser.parse_args()
    allowed = sorted(os.sched_getaffinity(0))
    if args.cpu not in allowed or min(args.trials, args.operations) < 1:
        parser.error('caller CPU must be allowed; trials and operations must be positive')
    if args.reclaimer_cpu is None:
        args.reclaimer_cpu = next((cpu for cpu in allowed if cpu != args.cpu), args.cpu)
    if args.reclaimer_cpu not in allowed:
        parser.error('reclaimer CPU must be allowed')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    spec = json.loads((ROOT / STUDY / 'baseline.json').read_text())
    patch = ROOT / STUDY / 'baseline.patch'
    if digest(patch) != spec['patch_sha256']:
        raise ValueError('reverse patch does not match baseline.json')

    sources = {name: out / ('source-' + name) for name in ['candidate', 'baseline']}
    capture = ROOT / 'workbench/tools/capture.py'
    command = [sys.executable, capture, 'create', sources['candidate']]
    for name, hashes in spec['files'].items():
        command += ['--expect', name + '=' + hashes['candidate']]
    run(command, out / 'candidate-capture.log')
    inputs = out / 'baseline-inputs'
    for name in spec['files']:
        path = inputs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(sources['candidate'] / name, path)
    # Exact pre/post hashes prohibit fuzzy reuse after production changes.
    apply = ['git', 'apply', '--unsafe-paths', '--directory=' + str(inputs), patch]
    # Run at the checkout root: Git otherwise silently filters paths outside
    # the current subdirectory, even with an explicit absolute --directory.
    run(apply[:2] + ['--check'] + apply[2:], out / 'patch-check.log', ROOT)
    run(apply, out / 'patch-apply.log', ROOT)
    command = [sys.executable, capture, 'create', sources['baseline'], '--base', sources['candidate']]
    for name, hashes in spec['files'].items():
        if digest(inputs / name) != hashes['baseline']:
            raise ValueError('reconstructed baseline hash mismatch: ' + name)
        command += ['--replace', name + '=' + str(inputs / name), '--expect', name + '=' + hashes['baseline']]
    run(command, out / 'baseline-capture.log')

    binaries, counters = {}, {}
    for name in ['baseline', 'candidate']:
        source = sources[name]
        counter = out / (name + '-counter-window.cpp')
        write_counter(source / 'orbital/src/allocation/window.cpp', counter, name == 'candidate')
        build = out / ('build-' + name)
        configure = ['cmake', '-S', source / STUDY, '-B', build, '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', '-DGROWTH_COUNTER_WINDOW=' + str(counter),
                     '-DGROWTH_HAS_INDEX=' + str(int(name == 'candidate')),
                     '-DSIXDB_MARCH=' + args.march, '-DSIXDB_TUNE=' + args.tune]
        if args.sanitizers:
            configure += ['-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer',
                          '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run(configure, out / (name + '-configure.log'))
        run(['cmake', '--build', build, '-j', os.environ.get('SIXDB_BUILD_JOBS', '2')],
            out / (name + '-build.log'))
        binaries[name] = build / 'growth_timing'
        counters[name] = build / 'growth_counters'
    options = ['--cpu', args.cpu, '--reclaimer-cpu', args.reclaimer_cpu, '--operations', args.operations]
    for name in binaries:
        execute(binaries[name], options + ['--check'], out / (name + '-check.csv'))
        execute(counters[name], options + ['--check'], out / (name + '-counter-check.csv'))

    rows, counter_rows, executions = [], [], []
    if not args.checks_only:
        for trial in range(args.trials):
            order = ['baseline', 'candidate'] if trial % 2 == 0 else ['candidate', 'baseline']
            for name in order:
                path = out / (name + '-' + str(trial) + '.csv')
                rows += [dict(variant=name, trial=trial, **row)
                         for row in execute(binaries[name], options, path)]
                executions.append(dict(variant=name, trial=trial, csv=path.name))
                print('measured', name, trial, flush=True)
        for name in counters:
            path = out / (name + '-counters.csv')
            counter_rows += [dict(variant=name, **row) for row in execute(counters[name], options, path)]
        write_csv(out / 'samples.csv', rows)
        write_csv(out / 'counters.csv', counter_rows)
        write_csv(out / 'summary.csv', summarize(rows))
    source_receipts = {}
    for name, source in sources.items():
        run([sys.executable, capture, 'verify', source], out / (name + '-capture-verify.log'))
        source_receipts[name] = json.loads(source.with_name(source.name + '.capture.json').read_text())['source_digest']
    context = dict(command=sys.argv, settings=vars(args), host=platform.uname()._asdict(),
        source_digests=source_receipts, reverse_patch_sha256=digest(patch),
        instrumented_window_sha256={name: digest(out / (name + '-counter-window.cpp')) for name in binaries},
        binary_sha256={name: digest(binary) for name, binary in binaries.items()},
        counter_binary_sha256={name: digest(binary) for name, binary in counters.items()},
        executions=executions, qualification='All fresh-Pool byte, placement, reuse and zero-charge close checks passed.',
        scope='Whole-cohort means, alternating paired fresh processes. Separate counter replay. Allocator accessible/idle/reserved accounting, not RSS; no individual-operation percentiles. Index node count and record sizeof are not total allocator heap footprint.')
    (out / 'context.json').write_text(json.dumps(context, indent=2, default=str) + '\n')
    print(out, flush=True)


if __name__ == '__main__':
    main()
