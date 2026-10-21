#!/usr/bin/env python3
"""Reduce paired API cleanup samples; retain latency, throughput and bytes separately."""
import argparse
import collections
import csv
import json
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('inputs', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
inputs = json.loads(args.inputs.read_text())
values = collections.defaultdict(list)
bytes_by_key = {}
for cohort in inputs['cohorts']:
    root = args.inputs.parent / cohort['path']
    for pattern in cohort['patterns']:
        paths = sorted(root.glob(pattern))
        if not paths:
            raise SystemExit(f'No samples matched {root / pattern}')
        for path in paths:
            profile, consumer, version = path.stem.split('-')[:3]
            for row in csv.DictReader(path.open()):
                operation = row['operation']
                if 'operations' in cohort and operation not in cohort['operations']:
                    continue
                if consumer == 'geometry':
                    bits, encoding, nodes = row['bits'], 'ends', '1'
                    footprint = (row['geometry_bytes'], row['payload_bytes'])
                else:
                    bits, encoding, nodes = '32', row['encoding'], row['nodes']
                    footprint = (row['encoded_bytes'], row['metadata_backing'], row['payload_backing'], row['view_bytes'])
                key = (cohort['platform'], profile, cohort['role'], consumer, bits, encoding,
                       row['count'], nodes, operation, row['concurrency'], row['method'], version)
                values[key].append(float(row['ns_request']))
                if key in bytes_by_key:
                    assert bytes_by_key[key] == footprint
                bytes_by_key[key] = footprint

args.output.mkdir(parents=True, exist_ok=True)
fields = ['platform', 'profile', 'cohort', 'consumer', 'bits', 'encoding', 'count', 'nodes',
          'operation', 'concurrency', 'method', 'version', 'samples', 'median_ns_request',
          'min_ns_request', 'max_ns_request', 'bytes']
with (args.output / 'summary.csv').open('w') as stream:
    writer = csv.writer(stream)
    writer.writerow(fields)
    for key, samples in sorted(values.items()):
        writer.writerow([*key, len(samples), f'{statistics.median(samples):.6f}',
                         f'{min(samples):.6f}', f'{max(samples):.6f}', '/'.join(bytes_by_key[key])])

# Absolute per-operation scorecards, one C1 and C8 column per implementation.
lines = ['# API cleanup: absolute complete-consumer samples', '',
         'Medians in ns/request. C1 is dependent latency; C8 is reciprocal throughput',
         'from eight independent requests. Neither is batch latency. Bytes are logical',
         'encoded bytes and granted backing, not an inferred cache-residence label.', '']
groups = sorted({k[:9] for k in values})
last = None
for group in groups:
    platform, profile, role, consumer, bits, encoding, count, nodes, operation = group
    if count != '256' or nodes != '1':
        continue
    section = group[:4]
    if section != last:
        lines.append('')
        lines += [f'## {platform} / {profile} / {role} / {consumer}', '',
                  '| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |',
                  '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
        last = section
    def at(method, version, c):
        rows = values.get(group + (str(c), method, version), [])
        return f'{statistics.median(rows):.2f}' if rows else '—'
    def fused(c):
        rows = values.get(group + (str(c), 'independent_fused', 'before'), []) + values.get(group + (str(c), 'independent_fused', 'after'), [])
        return f'{statistics.median(rows):.2f}' if rows else '—'
    method = 'geometry' if consumer == 'geometry' else 'streampack'
    numbers = [at(method, 'before', 1), at(method, 'after', 1), fused(1),
               at(method, 'before', 8), at(method, 'after', 8), fused(8)]
    label = {'0': 'Point → rank → payload', '1': 'Range → window → payload', '2': 'Rank → bounds → payload', 'value_chain': 'Value by rank', 'reverse_value_chain': 'Typed reverse → value', 'ordinary_reverse_value_chain': 'Ordinary reverse → value', 'raw_geometry_value_chain': 'Raw point → value', 'selected_geometry_value_chain': 'Selected point → value', 'selected_overlap_value_chain': 'Selected range → value'}.get(operation, operation)
    source_label = f'u{bits}' if consumer == 'geometry' else {'0': 'Absolute Ends', '1': 'Sizes', '2': 'Relative Ends'}[encoding]
    lines.append(f'| {source_label} | {label} | ' + ' | '.join(numbers) + ' |')
(args.output / 'scorecard.md').write_text('\n'.join(lines) + '\n')
print(f'{len(values)} series / {sum(map(len, values.values()))} samples; {args.output}')
