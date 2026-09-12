#!/usr/bin/env python3
"""Compare matching GPR/SIMD consumers; inputs are collected worker job directories."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('jobs', type=Path, nargs='+')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
report = {'metric': 'Median CPU ns per original row, including inactive rows', 'profiles': {}}
for job in args.jobs:
    for path in sorted((job / 'results').glob('*/samples.json')):
        samples = defaultdict(list)
        for row in json.loads(path.read_text())['benchmarks']:
            if row.get('error_occurred'):
                raise ValueError(f'{path}: {row}')
            if row['run_type'] == 'iteration':
                assert row['time_unit'] == 'ns'
                samples[row['run_name']].append(row['cpu_time'] / row['items_per_iteration'])
        median = {k: statistics.median(v) for k, v in samples.items()}
        groups = {}
        for access in ('random', 'scan'):
            for rows in (1, 2, 4, 8):
                for consumer in ('word_hash', 'sum', 'update'):
                    for control in ('point', 'gpr_points', 'simd', 'simd_full'):
                        for candidate in ('gpr_inline', 'gpr_compiled'):
                            ratios = []
                            for name, value in median.items():
                                parts = name.split('/')
                                if (int(parts[1]) != rows or parts[5] != access or
                                        parts[7] != consumer or parts[8] != candidate):
                                    continue
                                peer = '/'.join(parts[:-1] + [control])
                                if peer in median:
                                    ratios.append((value / median[peer], name, value, median[peer]))
                            if not ratios:
                                continue
                            ordered = sorted(ratios)
                            key = f'{access}/{rows}/{consumer}/{candidate}-over-{control}'
                            groups[key] = {
                                'pairs': len(ratios),
                                'median_ratio': statistics.median(r[0] for r in ratios),
                                'below_0_8': sum(r[0] < .8 for r in ratios),
                                'above_1_4': sum(r[0] > 1.4 for r in ratios),
                                'best': ordered[0], 'worst': ordered[-1],
                            }
        report['profiles'][path.parent.name] = {
            'job': job.name,
            'samples_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'cases': len(samples), 'groups': groups,
        }
if args.output:
    args.output.write_text(json.dumps(report, indent=2) + '\n')
else:
    print(json.dumps(report, indent=2))
