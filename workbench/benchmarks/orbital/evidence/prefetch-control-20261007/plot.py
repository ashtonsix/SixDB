#!/usr/bin/env python3
"""Regenerate this selected comparison; requires matplotlib only for plotting."""
import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--inputs', type=Path, default=Path(__file__).resolve().parent,
                    help='Complete selected inputs recovered from the evidence archive')
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
args = parser.parse_args()
ROOT = args.inputs.resolve()
OUTPUT = args.output.resolve()
OUTPUT.mkdir(parents=True, exist_ok=True)
for name in ('recovered-native-manual', 'recovered-ring', 'memory-reference', 'graviton-memory-reference'):
    bundle = ROOT / name
    provenance = json.loads((bundle / 'provenance.json').read_text())
    for relative, wanted in provenance['files_sha256'].items():
        actual = hashlib.sha256((bundle / relative).read_bytes()).hexdigest()
        if actual != wanted:
            raise ValueError(f'Retained input changed: {name}/{relative}')

memory = defaultdict(list)
intervals = [json.loads(line) for line in
             (ROOT / 'memory-reference/memory-pmu/intervals.jsonl').read_text().splitlines()]
if len({entry['source_file'] for entry in intervals}) != len(intervals):
    raise ValueError('Duplicate retained memory interval')
for entry in intervals:
    row = entry['record']
    group = row['measurement']
    event = next(e for e in group['events'] if e['name'] == 'l1d-load-misses')
    if (group['status'] != 'complete' or group['coverage'] != 1 or group['multiplexed']
            or event['status'] != 'measured' or event['count'] != 0):
        raise ValueError(f'Counter qualification changed: {entry["source_file"]}')
    memory[row['bytes'], row['active_chains']].append(row['elapsed_ns'] / row['loads'])
if len(intervals) != 360 or len(memory) != 40 or any(len(v) != 9 for v in memory.values()):
    raise ValueError('Incomplete memory reference')

composed = {}
identities = {}
binary_hashes = set()
for execution in ('native', 'manual', 'ring'):
    bundle = 'recovered-ring' if execution == 'ring' else 'recovered-native-manual'
    directory = ROOT / bundle / f'orbital-allocation/paired-{execution}'
    run = json.loads((directory / 'run.json').read_text())
    binary_hashes.add(run['binary_sha256'])
    rows = list(csv.DictReader((directory / 'cases.csv').open()))
    if run['status'] != 'complete' or len(rows) != 162:
        raise ValueError(f'Incomplete composed arm: {execution}')
    for row in rows:
        identity = tuple(row[k] for k in ('workload', 'records', 'mode', 'backend', 'trial'))
        fingerprint = tuple(row[k] for k in ('input_fingerprint', 'trace_fingerprint', 'final_fingerprint'))
        if identities.setdefault(identity, fingerprint) != fingerprint or row['completed_requests'] != row['requests']:
            raise ValueError('Unmatched trace/state/completion')
    for workload in ('point-hit', 'point-miss', 'dependent-16'):
        selected = [r for r in rows if r['backend'] == 'packed-owner' and r['workload'] == workload
                    and r['mode'] == 'throughput' and r['records'] == '4194304']
        if len(selected) != 3 or {r['trial'] for r in selected} != {'0', '1', '2'}:
            raise ValueError('Incomplete selected trials')
        composed[execution, workload] = median(float(r['elapsed_ns']) / int(r['completed_requests']) for r in selected)
if binary_hashes != {'72cdcbc5386510f6c8d0c715835c63373d0785ec05128b35e521c0c53832582d'}:
    raise ValueError('Frozen binary changed')

summary = {
    'format': 1, 'host': 'Same c8a.xlarge guest, EPYC 9R45, selected CPU 0, reported LLC 16 MiB',
    'composed_source_sha256': '0279e3caa86632a6345c9b3aaf21abf94f5427ca90f6fa022a10cc3962247da1',
    'composed_binary_sha256': next(iter(binary_hashes)),
    'observed_load_service_tier': None,
    'counter_qualification': {'l1d-load-misses': 'unqualified: full coverage but zero in all 360 memory intervals'},
    'memory': [{'bytes': size, 'active_chains': k, 'samples': len(v), 'median_ns_per_chase_load': median(v)}
               for (size, k), v in sorted(memory.items())],
    'composed': [{'execution': execution, 'workload': workload, 'records': 4194304,
                  'trials': 3, 'offered_batch': 64, 'active_width': 16,
                  'median_ns_per_logical_request': value}
                 for (execution, workload), value in composed.items()],
    'limits': ['Chase loads and logical requests are different units.',
               'Composed arms ran sequentially on one guest; no cross-arm joint shuffle.',
               'Batch completion quantiles are not scalar request p50/p99.',
               'Initialised/validated data and calibration are access history; neither suite flushes data.',
               'Footprint and timing alone do not attribute service to LLC or DRAM.'],
}
(OUTPUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')

plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(1, 2, figsize=(12, 4.8), layout='constrained')
labels = {16384: '16 KiB', 524288: '512 KiB', 8388608: '8 MiB', 268435456: '256 MiB'}
for size, label in labels.items():
    ks = sorted(k for s, k in memory if s == size)
    axes[0].plot(ks, [median(memory[size, k]) for k in ks], marker='o', ms=4, label=label)
axes[0].set(xscale='log', yscale='log', xlabel='Independent dependent chains (K)',
            ylabel='Median ns / chase load', title='Shuffled-cycle memory reference')
axes[0].set_xticks([1, 2, 4, 8, 16, 32, 64], labels=['1', '2', '4', '8', '16', '32', '64'])
axes[0].legend(title='Mapped footprint', frameon=False)
workloads = ('point-hit', 'point-miss', 'dependent-16')
for i, execution in enumerate(('native', 'manual', 'ring')):
    axes[1].bar([j + (i - 1) * .25 for j in range(3)],
                [composed[execution, w] for w in workloads], width=.23, label=execution)
axes[1].set(yscale='log', ylabel='Median ns / logical request', title='Frozen composed control: 4.2M rows')
axes[1].set_xticks(range(3), labels=['Hit', 'Miss', 'Chain ×16'])
axes[1].legend(frameon=False)
for ax in axes:
    ax.grid(axis='y', alpha=.2)
    ax.set_axisbelow(True)
fig.suptitle('Interleaving gains are observed; cache / DRAM service remains unqualified', fontsize=13)
fig.savefig(OUTPUT / 'comparison.png', dpi=180)
fig.savefig(OUTPUT / 'comparison.svg')
