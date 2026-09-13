#!/usr/bin/env python3
"""Retain conditional whole-consumer comparisons, not inferred prefetch counts."""
import csv
import json
from collections import defaultdict
from pathlib import Path
import statistics
import sys


def report(root):
    hardware = json.loads((root / 'hardware.json').read_text())
    plan = json.loads((root / 'plan.json').read_text())
    rows = list(csv.DictReader((root / 'samples.csv').open()))
    keys = ['footprint', 'rows', 'phase', 'extension_phase', 'pattern', 'k', 'extension_denominator',
            'compute_rounds', 'prefetch_extension', 'layout']
    grouped = defaultdict(list)
    for row in rows:
        grouped[tuple(row[k] for k in keys)].append(row)
    cases = []
    for key, samples in sorted(grouped.items()):
        timings = [float(s['ns_per_operation']) for s in samples]
        cases.append(dict(zip(keys, key)) | {'samples': len(samples),
            'seeds': sorted({int(s['seed']) for s in samples}),
            'ns_per_operation_median': statistics.median(timings),
            'ns_per_operation_min': min(timings), 'ns_per_operation_max': max(timings),
            'batches_below_target': sum(int(s['elapsed_ns']) < int(s['target_ns']) for s in samples),
            'allocated_bytes': int(samples[0]['allocated_bytes']),
            'useful_bytes_per_operation': int(samples[0]['useful_bytes']) / int(samples[0]['operations']),
            'model_lines_per_operation': statistics.median(int(s['model_lines']) / int(s['operations']) for s in samples),
            'model_lines_per_operation_range': [min(int(s['model_lines']) / int(s['operations']) for s in samples),
                                               max(int(s['model_lines']) / int(s['operations']) for s in samples)],
            'model_pages_per_operation': statistics.median(int(s['model_pages']) / int(s['operations']) for s in samples)})
    result = {'format': 1, 'hardware_lookup_key': hardware['lookup_key'], 'line_bytes': hardware['line_bytes'],
        'cases': cases, 'limitations': ['Batch CLOCK_MONOTONIC_RAW wall timing; no per-load timers or PMU traffic.',
            'Line/page demand counts are per-operation address unions, not cache misses or transferred bytes.',
            'padded128 specifies a 128B stride; strict 128B row alignment requires a base phase divisible by 128.',
            'Compute rounds produce a consumed result, not a calibrated pre-load delay: extension addresses/conditions '
            'can be known earlier, so the compiler or CPU may issue their loads before the arithmetic completes.',
            'Footprints are labeled controls; capacity ratios alone do not establish residency.',
            'Idle sibling/background load is not enforced; placements are first-touch receipts.',
            'Only read consumers; no mutation, output materialization or migration costs.',
            'Sample min/max are observed ranges, not confidence intervals.']}
    (root / 'analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    text = ['# Spatial consumer comparison', '',
        f"Reported line size: **{hardware['line_bytes']} B**; architecture: {hardware['recognised_architecture'] or 'unrecognised'}. "
        'All layouts contain the same 96B logical record. Timings are complete consumer-loop ns/operation.', '',
        plan['qualification'], '',
        'Page/NUMA observations before and after each placement are in `placement.txt`; '
        'mapping requests alone do not establish residency or NUMA placement.', '',
        '| Footprint | Core bytes / reported LLC | Logical bytes |', '| --- | ---: | ---: |']
    for item in plan['footprints']:
        ratio = item['core_bytes_over_reported_llc']
        text.append(f"| {item['name']} | {ratio:.3f} | {item['logical_bytes']} |" if ratio is not None else
                    f"| {item['name']} | unknown | {item['logical_bytes']} |")
    text += ['', 'Extension denominator: 0 = core only, 8 = one eighth, 1 = all. '
        'Phases remain separate; seeds/repetitions are summarized within each cell. '
        'Useful computation/prefetch/pattern are fixed by this run and retained in `analysis.json`.', '',
        '| Footprint | Phase | Extension | K | Layout | ns/op median [min, max] | Modeled lines/op | Allocated bytes |',
        '| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |']
    for c in cases:
        text.append(f"| {c['footprint']} | {c['phase']} | {c['extension_denominator']} | {c['k']} | {c['layout']} | "
            f"{c['ns_per_operation_median']:.3f} [{c['ns_per_operation_min']:.3f}, {c['ns_per_operation_max']:.3f}] | "
            f"{c['model_lines_per_operation']:.3f} | {c['allocated_bytes']} |")
    text += ['', '## Limits', ''] + ['- ' + x for x in result['limitations']] + ['']
    (root / 'summary.md').write_text('\n'.join(text))
    print(f'Summarized {len(rows)} samples into {len(cases)} conditional cells.')


if __name__ == '__main__':
    report(Path(sys.argv[1]))
