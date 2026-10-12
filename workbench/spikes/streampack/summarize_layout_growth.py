#!/usr/bin/env python3
"""Private metadata construction/growth; each sample includes initialization/free."""
import csv
from pathlib import Path
import statistics
import sys
from collections import defaultdict
source, output = map(Path, sys.argv[1:])
cells = defaultdict(list)
for path in source.glob('*/growth-[01].csv'):
    for row in csv.DictReader(path.open()):
        key = (path.parent.name, row['mode'], int(row['N']), int(row['maximum']), int(row['count']))
        cells[key].append(row)
assert cells
counts = ['allocations', 'growths', 'relocations', 'preserved_bytes', 'repacked_bytes', 'initialized_bytes', 'issued_bytes', 'capacity_bytes', 'requested_bytes']
with (output / 'growth-summary.csv').open('w') as file:
    writer = csv.writer(file)
    writer.writerow(['variant', 'mode', 'N', 'maximum', 'count', 'samples', 'median_ns_per_append', 'min_ns_per_append', 'max_ns_per_append', 'vs_fixed_prefix', 'checkpoint_bytes', 'length_origin_alignment'] + [f'{c}_{stat}' for c in counts for stat in ['median', 'min', 'max']])
    for key, rows in sorted(cells.items()):
        assert len(rows) == 6, (key, len(rows))
        times = [float(r['ns_per_append']) for r in rows]
        ref = cells[(key[0], 'fixed-prefix', *key[2:])]
        metrics = []
        for c in counts:
            values = [float(r[c]) for r in rows]
            metrics.extend([statistics.median(values), min(values), max(values)])
        writer.writerow([*key, len(rows), statistics.median(times), min(times), max(times), statistics.median(times) / statistics.median(float(r['ns_per_append']) for r in ref), rows[0]['checkpoint_bytes'], rows[0].get('length_alignment', 'unpadded'), *metrics])
print(f'{len(cells)} growth cells -> {output / "growth-summary.csv"}')
