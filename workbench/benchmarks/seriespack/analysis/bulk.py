#!/usr/bin/env python3
"""Compare individual SeriesPack bulk cases; retain gaps instead of averaging them away."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


SERIES = re.compile(r'bulk/series/([^/]+)/(local|striped)/k(\d+)/h(\d+)/u(\d+)/(encode|decode)$')


def read(path):
    document = json.loads(path.read_text())
    groups = defaultdict(list)
    for row in document['benchmarks']:
        if row.get('error_occurred'):
            raise ValueError(f"{path}: {row['name']}: {row.get('error_message')}")
        if row.get('run_type', 'iteration') != 'iteration':
            continue
        name = row.get('run_name', row['name'])
        if not name.startswith('bulk/'):
            continue
        speed = row['items_per_second']
        if not math.isfinite(speed) or speed <= 0:
            raise ValueError(f'{path}: invalid throughput for {name}')
        groups[name].append(row)
    cases = {}
    for name, rows in groups.items():
        extents = {row['logical_values'] for row in rows}
        if len(extents) != 1:
            raise ValueError(f'{path}: inconsistent logical extent for {name}')
        # Convert each repetition first: aggregate throughput and aggregate time
        # need not be reciprocals (notably the arithmetic mean).
        times = [1e9 / row['items_per_second'] for row in rows]
        if len({row.get('repetition_index', 0) for row in rows}) != len(rows):
            raise ValueError(f'{path}: duplicate repetitions for {name}')
        cases[name] = {
            'logical_values': extents.pop(),
            'repetitions': len(times),
            'median_ns_per_value': statistics.median(times),
            'min_ns_per_value': min(times),
            'max_ns_per_value': max(times),
        }
    if not cases:
        raise ValueError(f'{path}: no bulk repetitions')
    identity = {
        'path': str(path),
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'context': document.get('context', {}),
        'bulk_cases': len(cases),
    }
    return cases, identity


def comparison(candidate, control, name):
    if candidate['logical_values'] != control['logical_values']:
        raise ValueError(f'logical extent differs for {name}')
    return {
        'case': name,
        'median_ns_per_value': control['median_ns_per_value'],
        # Greater than one means the SeriesPack candidate takes more time.
        'time_ratio': candidate['median_ns_per_value'] / control['median_ns_per_value'],
    }


def summarize(cases, before, prior_target=None, predecessor_target=None):
    result = []
    for name, value in sorted(cases.items()):
        match = SERIES.fullmatch(name)
        if not match:
            continue
        target, layout, width, head, carrier, operation = match.groups()
        row = {'case': name, **value, 'target': target, 'layout': layout,
               'width': int(width), 'head': int(head), 'carrier': int(carrier),
               'operation': operation}
        suffix = f'k{width}/h0/u{carrier}/{operation}'
        prior_name = f'bulk/calico/{layout}/{suffix}'
        if prior_name in cases:
            row['same_layout_prior'] = comparison(value, cases[prior_name], prior_name)
            row['same_layout_prior']['target'] = prior_target
            row['same_layout_prior']['same_target'] = target == prior_target
        prior_names = [f'bulk/calico/{geometry}/{suffix}' for geometry in ('local', 'striped')]
        available = [key for key in prior_names if key in cases]
        if available:
            fastest = min(available, key=lambda key: cases[key]['median_ns_per_value'])
            row['fastest_prior_layout'] = comparison(value, cases[fastest], fastest)
            row['fastest_prior_layout']['target'] = prior_target
            row['fastest_prior_layout']['same_target'] = target == prior_target
        predecessor_name = f'bulk/predecessor/{layout}/{suffix}'
        if predecessor_name in cases and head == '0':
            row['same_wire_predecessor'] = comparison(value, cases[predecessor_name], predecessor_name)
            row['same_wire_predecessor']['target'] = predecessor_target
            row['same_wire_predecessor']['same_target'] = target == predecessor_target
        region_name = f'bulk/predecessor-region32/{layout}/{suffix}'
        if region_name in cases and head == '0':
            row['same_wire_predecessor_region32'] = comparison(value, cases[region_name], region_name)
            row['same_wire_predecessor_region32']['target'] = predecessor_target
            row['same_wire_predecessor_region32']['same_target'] = target == predecessor_target
        if target != 'scalar':
            scalar = f'bulk/series/scalar/{layout}/k{width}/h{head}/u{carrier}/{operation}'
            if scalar in cases:
                row['scalar'] = comparison(value, cases[scalar], scalar)
        if name in before:
            row['before'] = comparison(value, before[name], name)
        result.append(row)
    return result


def write_table(path, rows):
    """Compact per-case evidence; omitted controls remain empty cells."""
    fields = ['target', 'layout', 'width', 'head', 'carrier', 'operation',
              'logical_values', 'repetitions', 'median_ns_per_value',
              'min_ns_per_value', 'max_ns_per_value']
    controls = ['same_wire_predecessor', 'same_wire_predecessor_region32',
                'same_layout_prior', 'fastest_prior_layout', 'scalar', 'before']
    for control in controls:
        fields.extend(f'{control}_{part}' for part in
                      ('case', 'target', 'same_target', 'median_ns_per_value', 'time_ratio'))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fields)
        writer.writeheader()
        for row in rows:
            flat = {key: value for key, value in row.items() if key in fields}
            for control in controls:
                flat.update({f'{control}_{key}': value
                             for key, value in row.get(control, {}).items()})
            writer.writerow(flat)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    parser.add_argument('--before', type=Path, help='separate matched source checkpoint')
    parser.add_argument('--prior-target', choices=['scalar', 'neon', 'sve2', 'avx2', 'avx512'],
                        help='Calico target for older JSON without calico_target context; check its build receipt')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--table', type=Path, help='also write compact CSV of every SeriesPack case')
    parser.add_argument('--top', type=int, default=20, help='largest same-layout native/prior ratios to print')
    args = parser.parse_args()
    cases, identity = read(args.samples)
    before, before_identity = read(args.before) if args.before else ({}, None)
    recorded_target = identity['context'].get('calico_target')
    if recorded_target and args.prior_target and recorded_target != args.prior_target:
        parser.error('prior-target disagrees with the recorded Calico target')
    prior_target = recorded_target or args.prior_target
    if any(name.startswith('bulk/calico/') for name in cases) and prior_target is None:
        parser.error('older JSON needs --prior-target from its build receipt')
    rows = summarize(cases, before, prior_target, identity['context'].get('predecessor_target'))
    result = {
        'format': 1,
        'units': 'ns per logical value; time ratios greater than one are slower',
        'scope': 'CPU-time repetition medians, matched logical extent and carrier; '
                 'prior head is zero and its wire/placement differ. Fastest prior layout '
                 'compares another representation. Target labels identify execution families; '
                 'the separate build receipt specifies the allowed ISA. '
                 'No cache-residency or significance inference.',
        'samples': identity,
        'before_samples': before_identity,
        'cases': rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    if args.table:
        write_table(args.table, rows)
    print(f'{len(rows)} SeriesPack cases; {sum("same_layout_prior" in row for row in rows)} '
          f'with same-layout Calico; {sum("same_wire_predecessor" in row for row in rows)} '
          f'with same-wire predecessor; {sum("before" in row for row in rows)} with before data')
    for key in ('same_wire_predecessor', 'same_wire_predecessor_region32', 'same_layout_prior'):
        comparable = [row for row in rows if row['target'] != 'scalar' and row.get(key, {}).get('same_target')]
        if not comparable:
            continue
        print(key)
        for row in sorted(comparable, key=lambda row: row[key]['time_ratio'], reverse=True)[:args.top]:
            control = row[key]
            print(f"{control['time_ratio']:7.3f}x {row['median_ns_per_value']:.5f} / "
                  f"{control['median_ns_per_value']:.5f} ns/value  {row['case']}")


if __name__ == '__main__':
    main()
