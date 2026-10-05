"""Compare phase costs while exposing the full-snapshot fixture's actual work."""
import csv
import json
import math
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
page = json.loads((root / 'orbital_views_probe.jsonl').read_text())['page_bytes']
expected = {(n * page, m, keep) for n in (1, 16, 256)
            for m in ('copy', 'cow', 'take') for keep in (0, 1)}
groups = {key: [] for key in expected}
for order in ('forward', 'reverse'):
    for trial in (1, 2, 3):
        path = root / f'{order}-{trial}.csv'
        rows = list(csv.DictReader(path.open()))
        keys = [(int(r['part_bytes']), r['method'], int(r['retain_homes'])) for r in rows]
        assert len(keys) == len(expected) and set(keys) == expected, path
        for key, row in zip(keys, rows):
            assert int(row['repeats']) == 60 and int(row['stage_bytes']) == 64, row
            assert int(row['source_read_bytes']) == key[0] == int(row['material_written_bytes']), row
            assert int(row['copy_bytes']) == (page if key[1] == 'copy' else 0), row
            assert int(row['mapped_bytes']) == (0 if key[1] == 'take' else page), row
            assert int(row['pool_new_maps']) == (1 if key[2] and key[1] != 'take' else 0), row
            assert int(row['pool_restored']) == (59 if key[2] and key[1] != 'take' else 0), row
            for field in ('cycle_ns', 'acquire_ns', 'capture_ns', 'seal_install_ns'):
                assert math.isfinite(float(row[field])) and float(row[field]) >= 0, row
            groups[key].append(row)
fields = list(next(iter(groups.values()))[0])
with (root / 'comparisons.csv').open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=fields)
    writer.writeheader()
    for key, rows in sorted(groups.items()):
        result = rows[0].copy()
        for field in ('cycle_ns', 'acquire_ns', 'capture_ns', 'seal_install_ns'):
            result[field] = round(statistics.median(float(r[field]) for r in rows), 3)
        writer.writerow(result)
print(json.dumps({'result': 'pass', 'cells': 18, 'trials_per_cell': 6,
                  'iterations_per_trial': 60, 'timing': 'median of trial means'}))
