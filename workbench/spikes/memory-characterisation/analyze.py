"""Summarise observations without turning knees into undocumented hardware counts."""
from __future__ import annotations
import csv
import json
import statistics as stats
import sys
from collections import defaultdict
from pathlib import Path


def analyse(output, write=True):
    hardware = json.loads((output / 'hardware.json').read_text())
    rows = list(csv.DictReader((output / 'samples.csv').open()))
    for row in rows:
        for name in ['bytes', 'k', 'stride', 'train', 'ahead', 'rep', 'n']:
            row[name] = int(row[name])
        for name in ['p10', 'median', 'p90']:
            row[name] = float(row[name])
    grouped = defaultdict(list)
    for row in rows:
        key = tuple(row.get(k, '') for k in ['condition', 'family', 'variant', 'bytes', 'k', 'stride', 'train', 'ahead'])
        grouped[key].append(row['median'])
    curves = defaultdict(list)
    for key, values in grouped.items():
        condition, family, variant, size, k, stride, train, ahead = key
        if family == 'mlp':
            curves[condition, variant, size].append({'k': k, 'ns_per_load': stats.median(values),
                'repetition_min': min(values), 'repetition_max': max(values)})
    mlp = []
    for (condition, variant, size), curve in curves.items():
        curve.sort(key=lambda x: x['k'])
        best = min(p['ns_per_load'] for p in curve)
        candidates = [p['k'] for p in curve if p['ns_per_load'] <= best * 1.1]
        knee = min(candidates)
        baseline = curve[0]['ns_per_load']
        mlp.append({'condition': condition, 'page_mode': variant, 'bytes': size,
            'smallest_k_within_10pct_of_best': knee, 'best_ns_per_load': best,
            'serial_ns_per_load': baseline, 'effective_latency_overlap': baseline / best,
            'right_censored': knee == curve[-1]['k'], 'curve': curve,
            'fixed_latency_model_consistent': baseline / best <= 1.25 * max(p['k'] for p in curve),
            'interpretation': 'empirical ring candidate; includes TLB, bandwidth, compiler and loop costs'})
    prefetch = []
    arms = defaultdict(dict)
    for row in rows:
        if ':' not in row['variant']:
            continue
        variant, arm = row['variant'].split(':')
        key = tuple(row.get(k, '') for k in ['condition', 'family', 'k', 'stride', 'train', 'ahead', 'rep']) + (variant,)
        arms[key][arm] = row
    for key, values in arms.items():
        if set(values) != {'hot', 'cold', 'trained'}:
            continue
        cold, hot, trained = (values[k] for k in ['cold', 'hot', 'trained'])
        gap = cold['median'] - hot['median']
        separated = cold['p10'] > hot['p90'] * 1.2 and gap > 0
        score = (cold['median'] - trained['median']) / gap if separated else None
        condition, family, k, stride, train, ahead, rep, variant = key
        prefetch.append({'condition': condition, 'family': family, 'variant': variant,
            'k': k, 'stride': stride, 'train': train, 'ahead': ahead, 'rep': rep,
            'controls_separated': separated, 'cold_ns': cold['median'], 'hot_ns': hot['median'],
            'trained_ns': trained['median'], 'normalised_saving': score,
            'evidence': 'faster-than-cold' if score is not None and score >= .3 else
                        'no-clear-benefit' if separated else 'unresolved-controls-overlap'})
    result = {'format': 1, 'lookup_key': hardware['lookup_key'], 'hardware_identity': hardware['identity'],
              'mlp': mlp, 'prefetch': prefetch,
              'unresolved': ['exact miss-slot count', 'physical prefetch table size', 'cache-to-cache route']}
    if write:
        (output / 'analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    text = ['# Memory characterisation results', '',
            f"Architecture: {hardware['recognised_architecture'] or 'unrecognised'}; CPU {hardware['cpu']}; "
            f"line {hardware['line_bytes']} B (sysfs); base page {hardware['pages']['base_bytes']} B.", '',
            'Times are measured nanoseconds; MLP is nanoseconds per demand load. Knees are workload candidates.', '',
            '| Condition | Pages | MiB | Serial ns | Best ns/load | K within 10% | Effective overlap |',
            '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    for point in mlp:
        text.append(f"| {point['condition']} | {point['page_mode']} | {point['bytes']/2**20:g} | "
                    f"{point['serial_ns_per_load']:.2f} | {point['best_ns_per_load']:.2f} | "
                    f"{point['smallest_k_within_10pct_of_best']}{'+' if point['right_censored'] else ''} | "
                    f"{point['effective_latency_overlap']:.2f} |")
    text += ['', '## Prefetch diagnostics', '',
             'Scores normalise the trained median between same-case cold (0) and hot (1) controls. '
             'A score ≥0.3 indicates observed latency benefit; it is not a hit probability. '
             'Overlapping controls produce no inference.', '',
             '| Condition / family | Repetitions | Separated controls | Observed benefit |',
             '| --- | ---: | ---: | ---: |']
    families = defaultdict(list)
    for point in prefetch:
        families[point['condition'], point['family']].append(point)
    for (condition, family), points in families.items():
        text.append(f"| {condition} / {family} | {len(points)} | "
                    f"{sum(p['controls_separated'] for p in points)} | "
                    f"{sum(p['evidence'] == 'faster-than-cold' for p in points)} |")
    text += ['', '## Core sharing', '', '| Condition | Case | Median ns |', '| --- | --- | ---: |']
    for key, values in grouped.items():
        if key[1] in ['handoff', 'sharing']:
            text.append(f'| {key[0]} | {key[1]} / {key[2]} / offset {key[5]} | {stats.median(values):.2f} |')
    text += ['', 'Single-load handoffs include serialization and timer overhead. Sharing rows are wall '
             'nanoseconds per atomic increment across both writers. Clean/dirty latency does not '
             'identify a particular L2/LLC/interconnect route.', '']
    if write:
        (output / 'summary.md').write_text('\n'.join(text))
    return result


if __name__ == '__main__':
    analyse(Path(sys.argv[1]))
