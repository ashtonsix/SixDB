#!/usr/bin/env python3
"""Regenerate comparison tables from retained repetitions and accounting."""
import csv
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from evidence import read_measurements


def main():
    root = Path(sys.argv[1])
    receipt, benchmark = read_measurements(root)
    if receipt['status'] not in ('running', 'complete'):
        raise ValueError('Run did not succeed')
    accounting = list(csv.DictReader((root / 'accounting.csv').open()))
    keys = ('case', 'profile', 'method', 'phase')
    expected = {'/'.join(r[k] for k in keys): r for r in accounting}
    if not expected or len(expected) != len(accounting):
        raise ValueError('Empty or duplicate accounting selection')
    groups = {label: [] for label in expected}
    for sample in benchmark['benchmarks']:
        if sample.get('run_type') != 'iteration':
            continue
        label = '/'.join(sample['name'].split('/')[:4])
        if sample.get('error_occurred') or label not in groups or sample['time_unit'] != 'ns':
            raise ValueError(f'Unexpected benchmark sample: {sample}')
        groups[label].append(sample)
    output = []
    for label, samples in groups.items():
        record = expected[label]
        if record['correct'] != '1' or len(samples) != receipt['config']['repetitions']:
            raise ValueError(f'Incomplete/incorrect comparison: {label}')
        if len({r['repetition_index'] for r in samples}) != len(samples):
            raise ValueError('Duplicate repetition')
        if any(int(s['operations']) != int(record['operations']) for s in samples):
            raise ValueError('Operation denominator mismatch')
        values = [s['real_time'] / s['operations'] for s in samples]
        median = statistics.median(values)
        output.append(record | {'ns_per_operation': median, 'spread_percent': 100 * (max(values) - min(values)) / median})
    with (root / 'summary.csv').open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(output[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(output)
    text = ['# Trie-remapping measurements', '', 'Manual wall-clock ns per point query, scan request, delete/insert cycle, or rebuilt row.', 'Scan requests project one column; point queries read the configured row width.', 'Footprints are owned buffer capacities, excluding allocator overhead; container summaries are included in object sizes.', '', '| Case | Profile | Method | Phase | ns/op | Spread | Churn payload moved | Churn locator repairs |', '| --- | --- | --- | --- | ---: | ---: | ---: | ---: |']
    for r in output:
        text.append(f'| {r["case"]} | {r["profile"]} | {r["method"]} | {r["phase"]} | {r["ns_per_operation"]:.2f} | {r["spread_percent"]:.1f}% | {r["payload_moved"]} | {r["references_repaired"]} |')
    (root / 'summary.md').write_text('\n'.join(text) + '\n')
    print(f'{len(output)} comparisons, all repetitions and oracle records present.')


if __name__ == '__main__':
    main()
