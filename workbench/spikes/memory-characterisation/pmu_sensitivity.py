#!/usr/bin/env python3
"""Summarize observed counter response without assigning a memory-service tier."""
import argparse
import json
from pathlib import Path
import statistics


def analyze(directory):
    rows = [json.loads(p.read_text()) for p in sorted(directory.glob('*.json'))]
    if not rows:
        raise ValueError('no counter intervals')
    cells, events = {}, {}
    for row in rows:
        if row['loads'] <= 0 or row['elapsed_ns'] <= 0:
            raise ValueError('empty or invalid dependent chase')
        cells.setdefault((row['bytes'], row['active_chains']), []).append(row)
        for event in row['measurement']['events']:
            key = (event['name'], event['type'], event['config'])
            events.setdefault(key, []).append((row, event))
    summary = []
    for (name, kind, config), values in sorted(events.items()):
        valid = [(r, e) for r, e in values if r['measurement']['status'] == 'complete'
                 and r['measurement']['coverage'] == 1 and e['status'] == 'measured']
        counts = [e['count'] for _, e in valid]
        all_zero = bool(counts) and len({r['bytes'] for r, _ in valid}) > 1 and not any(counts)
        summary.append({'name': name, 'type': kind, 'config': config,
                        'intervals': len(values), 'complete_full_coverage_intervals': len(valid),
                        'observed_count_min': min(counts) if counts else None,
                        'observed_count_max': max(counts) if counts else None,
                        'observation': 'unresponsive_zero' if all_zero else 'counts_observed' if counts else 'unavailable_or_inadequate_coverage',
                        'qualification': 'unqualified' if all_zero or not counts else 'requires_CPU_kernel_event_semantics'})
    measurements = []
    for (size, chains), samples in sorted(cells.items()):
        measurements.append({'bytes': size, 'active_chains': chains, 'intervals': len(samples),
                             'median_ns_per_chase_load': statistics.median(r['elapsed_ns'] / r['loads'] for r in samples)})
    return {'schema': 1, 'intervals': len(rows), 'events': summary, 'cells': measurements,
            'observed_load_service_tier': None,
            'interpretation': 'counter response and dependent-chase costs; event-open success is not cache-residency proof'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(analyze(args.directory), indent=2) + '\n')


if __name__ == '__main__':
    main()
