#!/usr/bin/env python3
"""Median across fresh processes of each process's median ns/op, per build.

Usage: summarize.py RESULTS_DIR [--markdown]. RESULTS_DIR holds NAME-TRIAL.csv
files from cloud.sh. Ratios compare each variant with baseline on the same case.
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def load(directory):
    samples = defaultdict(lambda: defaultdict(list))  # case -> build -> [ns]
    order = []
    for path in sorted(Path(directory).glob('*-*.csv')):
        build = path.stem.rsplit('-', 1)[0]
        with path.open() as stream:
            for row in csv.DictReader(stream):
                case = row['case']
                if case not in order:
                    order.append(case)
                samples[case][build].append(float(row['median_ns']))
    return order, samples


def main():
    directory = sys.argv[1]
    markdown = '--markdown' in sys.argv
    order, samples = load(directory)
    builds = sorted({b for case in samples.values() for b in case}, key=lambda b: (b != 'baseline', b))
    header = ['case'] + [f'{b} ns' for b in builds] + [f'{b}/base' for b in builds if b != 'baseline']
    rows = []
    for case in order:
        medians = {b: statistics.median(v) for b, v in samples[case].items()}
        row = [case] + [f'{medians[b]:.2f}' if b in medians else '' for b in builds]
        for b in builds:
            if b == 'baseline':
                continue
            row.append(f'{medians["baseline"] / medians[b]:.2f}x' if b in medians and 'baseline' in medians else '')
        rows.append(row)
    if markdown:
        print('| ' + ' | '.join(header) + ' |')
        print('|' + '---|' * len(header))
        for row in rows:
            print('| ' + ' | '.join(row) + ' |')
    else:
        writer = csv.writer(sys.stdout)
        writer.writerow(header)
        writer.writerows(rows)


if __name__ == '__main__':
    main()
