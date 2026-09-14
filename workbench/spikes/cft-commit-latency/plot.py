#!/usr/bin/env python3
"""Static figures from compact CSVs; requires matplotlib 3.10.8."""
import argparse
import csv
from pathlib import Path
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import Normalize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = list(csv.DictReader((args.evidence / 'directed.csv').open()))
    tcp = [r for r in rows if r['protocol'] == 'tcp']
    azs = sorted({r['source'] for r in tcp})
    baseline = [r for r in tcp if int(r['mtu']) == 9001 and int(r['bytes']) == 64]
    arrays = []
    for metric in ['p50_us', 'p99_us']:
        values = np.full((6, 6), np.nan)
        for row in baseline:
            values[azs.index(row['source']), azs.index(row['destination'])] = float(row[metric]) / 1000
        arrays.append(values)
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 12, 'axes.spines.top': False, 'axes.spines.right': False})
    fig, axes = plt.subplots(1, 2, figsize=(11, 5.7), layout='constrained')
    cmap = plt.get_cmap('YlGnBu').copy(); cmap.set_bad('#f2f4f5')
    scale = Normalize(min(np.nanmin(a) for a in arrays), max(np.nanmax(a) for a in arrays))
    for ax, data, title in zip(axes, arrays, ['p50', 'p99']):
        im = ax.imshow(data, cmap=cmap, norm=scale)
        ax.set_xticks(range(6), [z.replace('use1-', '') for z in azs])
        ax.set_yticks(range(6), [z.replace('use1-', '') for z in azs])
        ax.set_xlabel('Destination AZ'); ax.set_ylabel('Initiating AZ')
        ax.set_title(title, fontweight='bold', pad=12)
        for i in range(6):
            for j in range(6):
                value = data[i, j]
                label = '—' if np.isnan(value) else f'{value:.3f}'
                color = '#ffffff' if not np.isnan(value) and scale(value) > .61 else '#142b3b'
                ax.text(j, i, label, ha='center', va='center', fontsize=10.5, color=color)
        ax.spines[['left', 'bottom']].set_visible(False); ax.tick_params(length=0)
    fig.colorbar(im, ax=axes, shrink=.75, label='Round-trip latency (ms)')
    fig.suptitle('Small-message p99 ranges from about 0.25 to 1.00 ms\n64-byte TCP request and reply · MTU 9001 · 9,000 samples/direction · three passes', fontsize=12)
    args.output.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output / 'az-rtt.png', dpi=180)
    plt.close(fig)
    sizes = [64, 512, 1400, 4096, 8192, 65536]
    indexed = {(int(r['mtu']), int(r['bytes']), r['source'], r['destination']): r for r in tcp}
    fig, axes = plt.subplots(1, 3, figsize=(11, 4.7), sharey=True, layout='constrained')
    for ax, metric, title in zip(axes, ['p50_us', 'p99_us', 'p999_us'], ['p50', 'p99', 'p99.9']):
        medians, q1, q3 = [], [], []
        for size in sizes:
            changes = sorted(100 * (float(indexed[9001, size, a, b][metric]) / float(indexed[1500, size, a, b][metric]) - 1)
                             for a in azs for b in azs if a != b)
            medians.append(statistics.median(changes)); q1.append(np.quantile(changes,.25)); q3.append(np.quantile(changes,.75))
        ax.axhline(0, color='#8b9298', linewidth=1)
        ax.vlines(range(6), q1, q3, color='#72a9b3', lw=5, label='Middle 50% of directions')
        ax.scatter(range(6), medians, s=32, color='#155b70', zorder=3, label='Median of 30 directions')
        ax.set_xticks(range(6), ['64 B', '512 B', '1400 B', '4 KiB', '8 KiB', '64 KiB'], rotation=40, ha='right', fontsize=10)
        ax.set_xlabel('Request and reply size'); ax.set_xlim(-.5, 5.5)
        ax.set_title(title, loc='left', weight='bold'); ax.grid(axis='y', alpha=.2)
    axes[0].set_ylabel('Change in RTT from MTU 1500 (%)')
    axes[0].legend(fontsize=8.5, loc='lower left', frameon=False)
    fig.suptitle('Jumbo frames help larger transfers; small-message effects stay near zero\nMTU 9001 vs 1500 · negative = lower RTT · intervals describe links, not confidence', fontsize=12)
    fig.savefig(args.output / 'mtu-effect.png', dpi=180)
    plt.close(fig)

if __name__ == '__main__':
    main()
