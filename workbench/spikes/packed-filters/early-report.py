#!/usr/bin/env python3
"""Summarize repeats without combining latency and reciprocal throughput."""
import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('input', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
keys = ['module', 'width', 'op', 'pattern', 'variant', 'metric']
groups = defaultdict(list)
context = {}
with a.input.open() as f:
    for row in csv.DictReader(f):
        key = tuple(row[k] for k in keys)
        groups[key].append(float(row['ns_window']))
        context[key] = [row['selected_fraction'], row['first_resolved_fraction']]
a.output.parent.mkdir(parents=True, exist_ok=True)
with a.output.open('w') as f:
    out = csv.writer(f)
    out.writerow(keys + ['median_ns', 'min_ns', 'max_ns', 'samples_ns',
                         'selected_fraction', 'first_resolved_fraction',
                         'current_ns', 'fused_ns', 'progressive_ns'])
    for key, values in groups.items():
        references = []
        for variant in ['current', 'fused', 'progressive']:
            other = key[:4] + (variant,) + key[5:]
            references.append(f'{statistics.median(groups[other]):.6f}' if other in groups else '')
        out.writerow(list(key) + [f'{statistics.median(values):.6f}', f'{min(values):.6f}',
            f'{max(values):.6f}', ';'.join(f'{v:.6f}' for v in values)] + context[key] + references)
