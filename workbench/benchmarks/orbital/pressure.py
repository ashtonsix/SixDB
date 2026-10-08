#!/usr/bin/env python3
"""Replay separately labeled allocation/pack lifecycle cohorts."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('--helper-cpu', type=int)
    parser.add_argument('--rounds', type=int, default=64)
    parser.add_argument('--slots', type=int, default=16)
    parser.add_argument('--arena-bytes', type=int, default=8 << 20)
    parser.add_argument('--repetitions', type=int, default=3)
    args = parser.parse_args()
    allowed = sorted(os.sched_getaffinity(0))
    helper = args.helper_cpu if args.helper_cpu is not None else next((c for c in allowed if c != args.cpu), args.cpu)
    if args.cpu not in allowed or helper not in allowed or args.repetitions < 1:
        parser.error('choose allowed CPUs and positive repetitions')
    root = args.output.resolve()
    if root.exists() and any(root.iterdir()):
        parser.error('output must be new or empty')
    root.mkdir(parents=True, exist_ok=True)
    measured = root / 'orbital_lifecycle_bench'
    shutil.copy2(args.binary, measured)
    binary_sha = digest(measured)
    context = {'format': 1, 'started_at': time.time(), 'binary_sha256': binary_sha,
               'allowed_cpus': allowed, 'caller_cpu': args.cpu, 'helper_cpu': helper,
               'config': vars(args) | {'binary': str(args.binary), 'output': str(root)}}
    (root / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    results = []
    variants = ['finite', 'window', 'mixed']
    for trial in range(args.repetitions):
        order = variants[trial % len(variants):] + variants[:trial % len(variants)]
        for runtime in order:
            leaf = root / f'{trial:02d}-{runtime}'
            command = [str(measured), '--output', str(leaf), '--runtime', runtime,
                       '--cpu', str(args.cpu), '--helper-cpu', str(helper),
                       '--rounds', str(args.rounds), '--slots', str(args.slots),
                       '--arena-bytes', str(args.arena_bytes)]
            print(f'Trial {trial}: lifecycle {runtime}', flush=True)
            subprocess.run(command, check=True)
            result = json.loads((leaf / 'result.json').read_text()) | {'trial': trial}
            if (not result['verified'] or result['offered_cycles'] != result['completed_cycles']
                    or result['after_shutdown_charged_bytes'] != 0):
                raise ValueError('incomplete or unverified lifecycle replay')
            result['cycles_per_second'] = result['completed_cycles'] * 1e9 / result['elapsed_ns']
            results.append(result)
    if digest(measured) != binary_sha:
        raise ValueError('measured binary changed during campaign')
    with (root / 'cases.csv').open('w') as out:
        writer = csv.DictWriter(out, fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    summary = []
    for runtime in variants:
        rows = [r for r in results if r['runtime'] == runtime]
        if len({r['checksum'] for r in rows}) != 1:
            raise ValueError('repetitions have different logical results')
        fields = ['cycles_per_second', 'cycle_p50_ns', 'cycle_p99_ns', 'drain_ns',
                  'observed_peak_charged_bytes', 'observed_peak_metadata_bytes',
                  'after_trim_charged_bytes', 'minor_faults', 'relocations',
                  'foreign_returns', 'trim_pages']
        summary.append({'runtime': runtime, 'trials': len(rows)} |
                       {field: statistics.median(r[field] for r in rows) for field in fields})
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    context['completed_at'] = time.time()
    context['verified'] = True
    (root / 'context.json').write_text(json.dumps(context, indent=2) + '\n')


if __name__ == '__main__':
    main()
