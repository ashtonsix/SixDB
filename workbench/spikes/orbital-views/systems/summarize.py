#!/usr/bin/env python3
"""Validate equal-work cells and summarize the six retained trial means."""
import csv
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
for family, keys, work, metrics, expected in [
    ('resource', ['source', 'group_parts', 'retention_shape', 'parts', 'part_bytes'],
     ['retained_parts', 'retained_backing_bytes', 'retained_useful_bytes', 'checksum'],
     ['source_init_ns', 'prepare_and_drop_ns', 'native_pair_ns', 'fd_delta', 'vma_delta',
      'rss_delta_kib', 'virtual_delta_kib'], 48),
    ('journal', ['packing', 'event_bytes', 'batch', 'events'],
     ['write_syscalls', 'sync_syscalls', 'user_copied_bytes', 'wire_bytes'],
     ['port_ns_per_event', 'max_service_batch_ns'], 18),
]:
    groups = {}
    paths = sorted(root.glob(f'{family}-forward-*.csv')) + sorted(root.glob(f'{family}-reverse-*.csv'))
    assert len(paths) == 6, (family, paths)
    reference = None
    for path in paths:
        rows = list(csv.DictReader(path.open()))
        assert len(rows) == expected, (path, len(rows))
        seen = set()
        for row in rows:
            key = tuple(row[k] for k in keys)
            assert key not in seen, (path, key)
            seen.add(key)
            groups.setdefault(key, []).append(row)
        assert reference is None or seen == reference, path
        reference = seen
    with (root / f'{family}-summary.csv').open('w') as stream:
        fields = keys + work + ['trials'] + [name + suffix for name in metrics for suffix in ('_median', '_min', '_max')]
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for key, rows in sorted(groups.items()):
            assert len(rows) == 6
            out = dict(zip(keys, key))
            for name in work:
                assert len({r[name] for r in rows}) == 1, (key, name)
                out[name] = rows[0][name]
            out['trials'] = 6
            for name in metrics:
                values = [float(r[name]) for r in rows]
                for suffix, result in [('median', statistics.median(values)), ('min', min(values)), ('max', max(values))]:
                    out[name + '_' + suffix] = round(result, 3)
            writer.writerow(out)
    print(f'{family}: {len(groups)} equal-work cells, six trials')
