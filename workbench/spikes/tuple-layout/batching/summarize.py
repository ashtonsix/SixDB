#!/usr/bin/env python3
"""Select the comparisons used by the packet findings from recovered worker bundles.

Arguments are paths to already fetched worker job directories (containing
artifact.json and results/). Raw sweeps, binaries and source stay in those bundles.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('kind', choices=['gather', 'outlined', 'before', 'small-demand', 'final', 'density'])
parser.add_argument('job', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--baseline', type=Path, help='retained implementation capture for density comparison')
args = parser.parse_args()
if (args.kind == 'density') != (args.baseline is not None):
    parser.error('density requires --baseline; other selections use one capture')
root = Path(__file__).resolve().parents[3]
# Select complete matched families, including masks and both operation kinds.
patterns = {
    'gather': r'^shape/(2|4)/(compact|spread)/',
    'outlined': r'^shape/(2|4)/(compact|spread)/',
    'before': r'^packets/(4/spread|8/one_byte|64/one_byte)/|^cross/(8/unit[^/]+/draw(2|4)/|16/unit3/draw2/).*/(packet|window)$',
    'small-demand': r'^cross/4/unit(24|32|64)/draw(2|4|8)/.*/spread/(sum|update)/(packet|point)$',
    'final': r'^(point|wide|scan|pipeline|composition)/|^packets/(4/spread|8/one_byte|64/one_byte)/|^cross/(8/unit[^/]+/draw(2|4)/|16/unit3/draw2/|2/unit[^/]+/draw1/|(2|4)/unit(24|32|64)/draw(2|4|8)/)',
    'density': r'^cross/(2|4)/unit(12|16|24|32)/draw(2|4|8)/',
}
inputs = []
for path in sorted((args.job / 'results').rglob('samples.json')):
    relative = path.relative_to(args.job / 'results')
    if args.kind in ('gather', 'outlined'):
        if 'module' in relative.parts or 'crossover' in relative.parts:
            continue
    elif 'probe' in relative.parts:
        continue
    if args.kind in ('small-demand', 'density') and 'crossover' not in relative.parts:
        continue
    label = '-'.join(relative.parts[:-1])
    if args.kind == 'density':
        label = 'density-' + label
    inputs.append(f'{label}={path}')
if args.baseline:
    for path in sorted((args.baseline / 'results' / 'crossover').glob('*/samples.json')):
        inputs.append(f'retained-crossover-{path.parent.name}={path}')
if not inputs:
    raise SystemExit('No completed measurement inputs found')
counters = ['items_per_iteration']
if args.kind in ('before', 'small-demand', 'final', 'density'):
    counters += ['physical_bytes', 'drawn_bytes', 'hull_bytes', 'window_bytes', 'stride']
options = [item for counter in counters for item in ('--counter', counter)]
subprocess.run([sys.executable, str(root / 'tools/evidence.py'), 'summarize', *inputs,
                '--output', str(args.output), '--filter', patterns[args.kind], *options,
                '--artifact', str(args.job / 'artifact.json')], check=True)
if args.baseline:
    (args.output / 'baseline-artifact.json').write_bytes((args.baseline / 'artifact.json').read_bytes())

if args.kind == 'final':
    coverage = {'metric': 'Median CPU ns per original row; packet / repeated prepared point',
                'profiles': {}}
    for path in sorted((args.job / 'results' / 'crossover').glob('*/samples.json')):
        samples = defaultdict(list)
        for row in json.loads(path.read_text())['benchmarks']:
            if row.get('error_occurred'):
                raise ValueError(f'Failed comparison in {path}: {row}')
            if row['run_type'] == 'iteration':
                assert row['time_unit'] == 'ns'
                samples[row['run_name']].append(row['cpu_time'] / row['items_per_iteration'])
        median = {k: statistics.median(v) for k, v in samples.items()}
        pairs = []
        for name, packet in median.items():
            if name.endswith('/packet'):
                point = median[name[:-6] + 'point']
                pairs.append({'case': name[:-7], 'rows': int(name.split('/')[1]),
                              'packet_ns': packet, 'point_ns': point, 'ratio': packet / point})
        by_rows = {}
        for rows in sorted({p['rows'] for p in pairs}):
            ratios = [p['ratio'] for p in pairs if p['rows'] == rows]
            by_rows[rows] = {'pairs': len(ratios), 'above_1_4': sum(r > 1.4 for r in ratios),
                             'median_ratio': statistics.median(ratios), 'max_ratio': max(ratios)}
        coverage['profiles'][path.parent.name] = {
            'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'cases': len(samples), 'pairs': len(pairs), 'by_rows': by_rows,
            'exceptions': sorted((p for p in pairs if p['ratio'] > 1.4),
                                 key=lambda p: p['ratio'], reverse=True)}
    (args.output / 'coverage.json').write_text(json.dumps(coverage, indent=2) + '\n')
