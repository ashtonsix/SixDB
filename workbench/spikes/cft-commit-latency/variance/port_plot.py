#!/usr/bin/env python3
"""Regenerate port-policy figure from the compact retained comparison table."""
import argparse
import csv
from pathlib import Path
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    p = argparse.ArgumentParser()
    p.add_argument('evidence', type=Path)
    args = p.parse_args()
    rows = list(csv.DictReader((args.evidence / 'port-comparisons.csv').open()))
    cross = [r for r in rows if r['same_az'] == 'False']
    primary = [r for r in cross if r['primary'] == 'True']
    azpairs = sorted({(int(r['az_a']), int(r['az_b'])) for r in primary})
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
    fig, (left, right) = plt.subplots(1, 2, figsize=(12, 7.5), gridspec_kw={'width_ratios': [1.15, 1]})
    for y, (a, b) in enumerate(azpairs):
        values = [float(r['holdout_worst_gain_us']) for r in primary if (int(r['az_a']), int(r['az_b'])) == (a, b)]
        left.scatter(values, [y + d for d in (-.18, -.06, .06, .18)], s=28,
                     c=['#137e8b' if v >= 0 else '#c17d31' for v in values], alpha=.85)
        left.plot(statistics.median(values), y, marker='|', markersize=16, color='#25394a', markeredgewidth=2)
    left.axvspan(-5, 5, color='#dfe4e8', alpha=.5, label='±5 µs')
    left.plot([], [], linestyle='none', marker='|', markersize=12, markeredgewidth=2,
              color='#25394a', label='Median of four')
    left.axvline(0, color='#74808a', linewidth=.8)
    left.set_yticks(range(len(azpairs)), [f'az{a}–az{b}' for a, b in azpairs])
    left.invert_yaxis()
    left.set_xlabel('Sequential − scattered RTT (µs)\nPositive favors scattered ports')
    left.set_title('Primary: 16 ports per policy\nFour fixed host pairs per AZ pair', loc='left', pad=15)
    left.grid(axis='x', alpha=.15)
    left.legend(loc='lower right', frameon=False, fontsize=9)
    for arm, color in [('sequential', '#c17d31'), ('scattered', '#137e8b')]:
        for budget, style in [(4, ':'), (16, '-'), (32, '--')]:
            values = sorted(float(r[f'{arm}_rtt_holdout_worst_us']) for r in cross if int(r['budget']) == budget)
            right.step(values, [(i + 1) / len(values) for i in range(len(values))], where='post',
                       color=color, linestyle=style, linewidth=1.8 if budget == 16 else 1.1,
                       label=f'{arm.capitalize()}, {budget}')
    right.set_xlabel('Selected RTT (µs)\nWorst of the two held-out block medians')
    right.set_ylabel('Fraction of the 60 observed cross-AZ host pairs')
    right.set_ylim(0, 1.02)
    right.grid(alpha=.15)
    right.set_title('Candidate budgets: 4 / 16 / 32\nAll winners chosen from training only', loc='left', pad=15)
    right.legend(frameon=False, fontsize=9, loc='lower right')
    fig.suptitle('Do scattered UDP ports find a better repeatable frontier?', x=.06, ha='left', fontsize=16, fontweight='bold')
    fig.text(.06, .025, '12 fresh hosts • 5 rounds • 40 measured echoes per block • RTT excludes peer turnaround\n'
             'Paired descriptive comparisons; correlated hosts/ports, one seed per pair. No IID-path or tail-latency claim.',
             fontsize=9, color='#4b5965')
    fig.subplots_adjust(left=.09, right=.98, bottom=.16, top=.86, wspace=.3)
    for extension in ('png', 'svg'):
        fig.savefig(args.evidence / f'port-sampling.{extension}', dpi=170, facecolor='white')
    plt.close(fig)


if __name__ == '__main__':
    main()
