#!/usr/bin/env python3
"""Derive calibrated rates without naming a cache service or DRAM traffic."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import statistics
from run import audit_sample


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('result', type=Path)
    a = p.parse_args()
    rows = [json.loads(line) for line in (a.result / 'samples.jsonl').read_text().splitlines()]
    identity = json.loads((a.result / 'identity.json').read_text())
    complete = json.loads((a.result / 'complete.json').read_text())
    assert len(rows) == complete['samples']
    groups = defaultdict(list)
    for row in rows:
        audit_sample(row)
        groups[(len(row['workers']), row['mode'], row['chains'], row['bytes_per_worker'],
                row.get('execution', 'fixed-batch'))].append(row)
    output = []
    duplicates = []
    for (cores, mode, chains, size, execution), values in sorted(groups.items()):
        arms = {arm: [r for r in values if r['arm'] == arm] for arm in ('primary', 'duplicate')}
        assert set(r['trial'] for r in arms['primary']) == set(r['trial'] for r in arms['duplicate'])
        total = lambda r: r['total_units'] if r['protocol'] >= 2 else r['units_per_worker'] * cores
        rate = lambda r: total(r) * 8 / r['wall_ns']
        line_rate = lambda r: total(r) * (r['line'] if mode == 'chase' else 8) / r['wall_ns']
        unit_ns = lambda r: r['wall_ns'] * cores / total(r)
        ratio = statistics.median(rate(r) for r in arms['primary']) / statistics.median(rate(r) for r in arms['duplicate'])
        duplicates.append(ratio)
        output.append(dict(cores=cores, mode=mode, chains=chains, bytes_per_worker=size,
            execution=execution, aggregate_mapped_body_bytes=size * cores,
            minimum_distinct_read_line_bytes_per_worker=min(w.get('distinct_read_line_bytes', size) for r in values for w in r['workers']),
            useful_GB_s=statistics.median(rate(r) for r in values),
            nominal_line_GB_s=statistics.median(line_rate(r) for r in values),
            ns_per_unit_per_core=statistics.median(unit_ns(r) for r in values),
            primary_duplicate_rate_ratio=ratio,
            minimum_overlap_fraction=min(r['overlap_ns'] / r['wall_ns'] for r in values),
            minimum_thread_running_fraction=min(w['thread_cpu_ns'] / (w['end_ns'] - w['start_ns']) for r in values for w in r['workers']),
            samples=len(values)))
    with (a.result / 'cells.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, list(output[0]), lineterminator='\n'); w.writeheader(); w.writerows(output)
    result = dict(cells=len(output), samples=len(rows),
        duplicate_rate_ratio_range=[min(duplicates), max(duplicates)],
        cache_service_observed=False, dram_traffic_measured=False,
        hardware=identity['hardware']['identity'], cells_csv='cells.csv')
    (a.result / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
