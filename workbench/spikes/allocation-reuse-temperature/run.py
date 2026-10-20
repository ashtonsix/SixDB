#!/usr/bin/env python3
"""Build frozen reuse candidates and run sequential actual-allocator comparisons."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
STUDY = Path('workbench/spikes/allocation-reuse-temperature')
ALLOCATION = Path('orbital/src/allocation')
POLICY_FILES = {
    'baseline': (),
    'segmented': ('internal.h', 'allocation.cpp', 'window.cpp'),
    'cascade-mru': ('internal.h', 'allocation.cpp', 'window.cpp'),
    'cascade-fifo': ('internal.h', 'allocation.cpp', 'window.cpp'),
    'cached': ('internal.h', 'window.cpp'),
    'window-first-ready': ('window.cpp',),
    'finite-two-pass': ('group.cpp',),
    'combined': ('window.cpp', 'group.cpp'),
}


def run(command, log):
    with log.open('w') as stream:
        subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, check=True)


def trial_order(policies, trial):
    # Pair both directions at each rotation before advancing the start policy.
    offset = (trial // 2) % len(policies)
    order = policies[offset:] + policies[:offset]
    return order if trial % 2 == 0 else list(reversed(order))


def touch_widths(value):
    if value is None:
        return [None]
    widths = []
    for width in value.split(','):
        if width != 'full':
            try:
                number = int(width)
            except ValueError:
                raise ValueError('--touch-bytes needs comma-separated full or nonnegative integer widths') from None
            if number < 0:
                raise ValueError('--touch-bytes needs comma-separated full or nonnegative integer widths')
            width = str(number) if number else 'full'
        widths.append(width)
    if len(widths) != len(set(widths)):
        raise ValueError('--touch-bytes must contain distinct widths (0 and full are the same width)')
    return widths


def source_changes(base, source):
    before = json.loads(base.with_name(base.name + '.capture.json').read_text())
    after = json.loads(source.with_name(source.name + '.capture.json').read_text())
    baseline = before['source_files_sha256']
    candidate = after['source_files_sha256']
    return sorted(name for name in baseline.keys() | candidate.keys()
                  if baseline.get(name) != candidate.get(name))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--policies', default='baseline,segmented,cascade-mru,cascade-fifo')
    parser.add_argument('--workloads', default='all', help='comma-separated allocator workload names')
    parser.add_argument('--touch-bytes', help='comma-separated touch widths, e.g. 64,full; default 64; 0 means full')
    parser.add_argument('--slots', type=int, default=256)
    parser.add_argument('--rounds', type=int, default=12)
    parser.add_argument('--operations', type=int, default=131072)
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--march', default='native')
    parser.add_argument('--tune', default='generic')
    parser.add_argument('--hot-bytes', type=int, default=256 * 1024)
    parser.add_argument('--warm-bytes', type=int, default=4 * 1024 * 1024)
    args = parser.parse_args()
    policies = args.policies.split(',')
    if not policies or len(set(policies)) != len(policies) or any(p not in POLICY_FILES for p in policies):
        parser.error('--policies must contain distinct names from ' + ','.join(POLICY_FILES))
    if min(args.slots, args.rounds, args.operations, args.trials) <= 0:
        parser.error('--slots, --rounds, --operations and --trials must be positive')
    try:
        widths = touch_widths(args.touch_bytes)
    except ValueError as error:
        parser.error(str(error))
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    capture = ROOT / 'workbench/tools/capture.py'
    base = out / 'source-baseline'
    run([sys.executable, capture, 'create', base, '--base', args.source.resolve()], out / 'capture.log')
    binaries = {}
    binary_hashes = {}
    changes = {}
    sources = {}
    source_digests = {}
    for policy in policies:
        source = base
        if policy != 'baseline':
            draft = out / ('draft-' + policy)
            for name in POLICY_FILES[policy]:
                p = ALLOCATION / name
                (draft / p).parent.mkdir(parents=True, exist_ok=True)
                (draft / p).write_bytes((base / p).read_bytes())
            run([sys.executable, base / STUDY / 'variant.py', draft, policy,
                 '--hot-bytes', args.hot_bytes, '--warm-bytes', args.warm_bytes], out / (policy + '-variant.log'))
            source = out / ('source-' + policy)
            command = [sys.executable, capture, 'create', source, '--base', base]
            for name in POLICY_FILES[policy]:
                p = ALLOCATION / name
                command += ['--replace', str(p) + '=' + str(draft / p)]
            run(command, out / (policy + '-capture.log'))
        changes[policy] = source_changes(base, source)
        expected = sorted(str(ALLOCATION / name) for name in POLICY_FILES[policy])
        if changes[policy] != expected:
            raise ValueError(f'{policy} source differences {changes[policy]} != expected {expected}')
        sources[policy] = source
        source_digests[policy] = json.loads(source.with_name(source.name + '.capture.json').read_text())['source_digest']
        build = out / ('build-' + policy)
        run(['cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DSIXDB_SPIKES=allocation-reuse-temperature', '-DSIXDB_MARCH=' + args.march,
             '-DSIXDB_TUNE=' + args.tune], out / (policy + '-configure.log'))
        run(['cmake', '--build', build, '--target', 'allocation_reuse_temperature_allocator',
             'orbital_validate', 'loom_validate', '-j', os.environ.get('SIXDB_BUILD_JOBS', '1')],
            out / (policy + '-build.log'))
        binary = build / STUDY / 'allocation_reuse_temperature_allocator'
        binaries[policy] = binary
        binary_hashes[policy] = hashlib.sha256(binary.read_bytes()).hexdigest()
        print('Built and qualified', policy, flush=True)
    samples = []
    orders = []
    executions = []
    for trial in range(args.trials):
        order = trial_order(policies, trial)
        orders.append(order)
        for policy in order:
            binary = binaries[policy]
            digest = binary_hashes[policy]
            for width in widths:
                assert digest == hashlib.sha256(binary.read_bytes()).hexdigest()
                stem = policy + '-' + str(trial)
                if len(widths) > 1:
                    stem += '-touch' + width
                path = out / (stem + '.csv')
                with path.open('w') as stream, (out / (stem + '.log')).open('w') as errors:
                    command = [str(binary), '--cpu', str(args.cpu), '--slots', str(args.slots),
                               '--rounds', str(args.rounds), '--operations', str(args.operations)]
                    if args.workloads != 'all':
                        command += ['--workloads', args.workloads]
                    if width is not None:
                        command += ['--touch-bytes', width]
                    subprocess.run(command, stdout=stream, stderr=errors, check=True)
                assert digest == hashlib.sha256(binary.read_bytes()).hexdigest()
                executions.append(dict(policy=policy, trial=trial, touch_bytes=width or '64',
                                       command=command, csv=path.name, binary_sha256=digest))
                with path.open() as stream:
                    for row in csv.DictReader(stream):
                        # Older captures touched one 64-byte line and did not
                        # emit this field; keep that default comparable.
                        row.setdefault('touch_bytes', '64')
                        samples.append(dict(policy=policy, trial=trial, binary_sha256=digest, **row))
                print('Measured', policy, trial, 'touch', width or '64', flush=True)
    columns = list(samples[0])
    with (out / 'samples.csv').open('w') as stream:
        writer = csv.DictWriter(stream, columns); writer.writeheader(); writer.writerows(samples)
    summaries = []
    for workload, touch_bytes in sorted({(s['workload'], s['touch_bytes']) for s in samples}):
        medians = {p: statistics.median(float(s['block_mean_ns_per_op']) for s in samples
                   if s['policy'] == p and s['workload'] == workload and s['touch_bytes'] == touch_bytes)
                   for p in policies}
        for policy in policies:
            summaries.append(dict(workload=workload, touch_bytes=touch_bytes, policy=policy,
                                  median_block_ns_per_op=medians[policy],
                                  ratio_to_baseline=medians[policy] / medians.get('baseline', medians[policy])))
    with (out / 'comparisons.csv').open('w') as stream:
        writer = csv.DictWriter(stream, list(summaries[0])); writer.writeheader(); writer.writerows(summaries)
    (out / 'context.json').write_text(json.dumps(dict(
        command=sys.argv, settings=vars(args) | {'output': str(out), 'source': str(args.source)},
        scope='Block mean allocation/touch/release cost; no individual-operation latency claim.',
        trial_orders=orders, executions=executions, policy_source_changes=changes,
        source_digests=source_digests, binary_sha256=binary_hashes,
        source_manifests={p: str(s.with_name(s.name + '.capture.json')) for p, s in sources.items()}
    ), indent=2, default=str) + '\n')


if __name__ == '__main__':
    main()
