#!/usr/bin/env python3
"""Tables from frame_bodies_columns output: ns per block (per point for point)
for the Frame, native narrow and u64 arms, with native / Frame, plus each
column's bytes and block census. Medians over repeats."""
import json, sys, statistics
from collections import defaultdict

cells = defaultdict(list)
meta = {}
for path in sys.argv[1:]:
    for line in open(path):
        d = json.loads(line)
        key = (d['isa'], d['rows'], d['policy'], d['column'], d['op'], d['variant'])
        cells[key + (d['arm'],)].append(d['ns'])
        meta[(d['isa'], d['rows'], d['policy'], d['column'])] = d
rows = defaultdict(dict)
for k, v in cells.items():
    rows[k[:-1]][k[-1]] = statistics.median(v)
print('isa rows policy column op variant frame native u64 native/frame')
for k in sorted(rows):
    r = rows[k]
    ratio = r['native'] / r['frame'] if r.get('frame') else float('nan')
    print(' '.join(map(str, k)), '%.2f %.2f %.2f %.2fx' % (r['frame'], r['native'], r['u64'], ratio))
print()
print('isa rows policy column frame_bytes native_bytes constant runs packed wide mean_runs')
for k in sorted(meta):
    d = meta[k]
    print(' '.join(map(str, k)), d['frame_bytes'], d['native_bytes'], d['constant'], d['runs'], d['packed'], d['wide'], d['mean_runs'])
