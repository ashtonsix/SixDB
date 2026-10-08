#!/usr/bin/env python3
"""Keep all matched dispatch cases and report local whole-visit timings."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = [json.loads(line) for line in args.input.read_text().splitlines()]
    semantic, timings = defaultdict(list), defaultdict(list)
    paths = {'fixed-inline', 'code-lut-switch', 'procedure-lut', 'continuation-lut'}
    for row in rows:
        assert row['event'] == 'sample' and row['path'] in paths
        assert row['elapsed_ns'] > 0 and row['visits'] > 0
        key = (row['pattern'], row['training'], row['pressure'], row['trial'])
        semantic[key].append(row)
        timings[key[:3] + (row['path'],)].append(row['elapsed_ns'] / row['visits'])
    for group in semantic.values():
        assert len(group) == 4 and {r['path'] for r in group} == paths
        assert len({(r['loads'], r['visits'], r['checksum'], r['width'], r['nodes']) for r in group}) == 1
    result = {'input_sha256': hashlib.sha256(args.input.read_bytes()).hexdigest(),
              'samples': len(rows), 'matched_semantic_groups': len(semantic),
              'units': 'whole timed native batch ns per dispatched visit',
              'limits': ['local ARM Linux VM wall-clock samples; no V2 or Zen5 hardware timings',
                         'uniform demand code within each batch; mixed-code predictor behavior unmeasured',
                         'no DRAM service attribution or policy optimality claim',
                         'table preparation and independent oracles excluded; admission and drain included'],
              'rows': []}
    for key, values in timings.items():
        result['rows'].append({'pattern': key[0], 'training': key[1], 'pressure': key[2], 'path': key[3],
                               'trials': len(values), 'median_ns_per_visit': statistics.median(values),
                               'range_ns_per_visit': [min(values), max(values)]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'samples': len(rows), 'matched_semantic_groups': len(semantic)}))


if __name__ == '__main__':
    main()
