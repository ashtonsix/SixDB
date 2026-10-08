#!/usr/bin/env python3
"""Render qualified scalar headroom, leaving unmeasured tiers visibly pending."""
import argparse
import hashlib
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('panel', type=Path)
parser.add_argument('--output', type=Path, help='Render into ignored output without changing inputs')
args = parser.parse_args()
output = args.output or args.panel
output.mkdir(parents=True, exist_ok=True)
qualification = json.loads((args.panel / 'qualification.json').read_text())
validator = json.loads((args.panel / 'summary-validator.json').read_text())
for key, name in [('cases_sha256', 'cases.csv'), ('comparison_sha256', 'comparison.json')]:
    assert hashlib.sha256((args.panel / name).read_bytes()).hexdigest() == validator[key]
assert qualification['validator_sha256'] == validator['runner_sha256']
assert hashlib.sha256((args.panel / 'owner-headroom.json').read_bytes()).hexdigest() == qualification['owner_headroom_sha256']
rows = json.loads((args.panel / 'owner-headroom.json').read_text())
rows = [r for r in rows if r['reference'] == 'lmdb-read-batch64']
assert all(r['latency_unit'] == 'logical request' for r in rows)
workloads = ['point-hit', 'point-miss', 'dependent-16', 'scan-16', 'stock-range',
             'order-status', 'point-update', 'point-rmw', 'insert-random', 'delete',
             'append', 'insert-scan', 'ycsb-a', 'ycsb-b', 'ycsb-c', 'ycsb-d',
             'ycsb-e', 'ycsb-f', 'order-entry']
sizes = [8192, 262144, 4194304]
values = {(r['workload'], r['records']): r for r in rows}
assert len(values) == len(rows)
assert len(rows) == len(workloads) * len(qualification['selected_record_tiers'])
rate = np.full((len(workloads), len(sizes)), np.nan)
tail = np.full_like(rate, np.nan)
for i, workload in enumerate(workloads):
    for j, size in enumerate(sizes):
        if (workload, size) in values:
            row = values[workload, size]
            rate[i, j] = row['throughput_ratio']
            tail[i, j] = row['p99_ns_headroom_ns'] / 1000
fig, axes = plt.subplots(1, 2, figsize=(10.8, 8.8), layout='constrained', sharey=True)
cmap = plt.get_cmap('RdBu').copy()
cmap.set_bad('#e4e6e9')
max_log = max(2, math.ceil(float(np.nanmax(np.log2(rate)))))
tail_range = max(3, float(np.nanmax(np.abs(tail))))
images = [axes[0].imshow(np.log2(rate), cmap=cmap,
                        norm=TwoSlopeNorm(vmin=-1, vcenter=0, vmax=max_log), aspect='auto'),
          axes[1].imshow(tail, cmap=cmap,
                        norm=TwoSlopeNorm(vmin=-tail_range, vcenter=0, vmax=tail_range), aspect='auto')]
labels = [label + ('\npending' if size not in qualification['selected_record_tiers'] else '')
          for label, size in zip(['8K rows', '262K rows', '4.2M rows'], sizes)]
for ax, title in zip(axes, ['Throughput / LMDB', 'LMDB p99 minus owner p99 (µs)']):
    ax.set_title(title, pad=14)
    ax.set_xticks(range(3), labels)
    ax.xaxis.tick_top()
    ax.set_yticks(range(len(workloads)), workloads)
    ax.tick_params(length=0)
    ax.spines[['left', 'right', 'top', 'bottom']].set_visible(False)
for i in range(len(workloads)):
    for j in range(3):
        if not np.isfinite(rate[i, j]):
            continue
        axes[0].text(j, i, f'{rate[i, j]:.2f}×', ha='center', va='center',
                     color='white' if math.log2(rate[i, j]) > max_log * .6 else '#19212c')
        axes[1].text(j, i, f'{tail[i, j]:+.2f}', ha='center', va='center',
                     color='white' if abs(tail[i, j]) > tail_range * .6 else '#19212c')
fig.colorbar(images[0], ax=axes[0], location='bottom',
             ticks=range(-1, max_log + 1), format=lambda value, _: f'{2.0 ** value:g}×')
fig.colorbar(images[1], ax=axes[1], location='bottom')
partial = qualification['original_worker_state'] == 'interrupted'
counts = {r['candidate_prepared_pages'] for r in rows}
assert len(counts) == 1
count = next(iter(counts))
treatment = f'Owner + {count} prepared Pages (producer CPU {rows[0]["producer_cpu"]})' if count else 'Synchronous owner allocation'
fig.suptitle(('Completed tiers from an interrupted worker' if partial else 'Selected allocation consumer') +
             '\n' + treatment + ' versus LMDB read snapshots up to 64 requests', fontsize=13)
fig.supxlabel('Three fixed-trace trial medians; LMDB writes use one transaction per request.\n'
              'Closed-loop request latency. Cache service unknown; database isolation/durability omitted.', fontsize=9)
fig.savefig(output / 'scalar-headroom.png', dpi=180)
fig.savefig(output / 'scalar-headroom.svg')
plt.close(fig)
print(output / 'scalar-headroom.png')
