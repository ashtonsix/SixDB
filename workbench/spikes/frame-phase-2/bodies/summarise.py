#!/usr/bin/env python3
"""Tables from frame_bodies_measure samples: ns per block (per point for point)
by tier, width and operation, one column per arm; n is runs or exceptions per
block. Medians over repeats."""
import json, sys, statistics
from collections import defaultdict

def main(paths, body='packed', ops=None, tiers=None):
    cells = defaultdict(list)
    for path in paths:
        for line in open(path):
            d = json.loads(line)
            if d['body'] != body:
                continue
            key = (d['isa'], d['tier'], d['k'], d.get('runs', d.get('exceptions', 0)), d['op'], d['variant'], d['arm'])
            cells[key].append(d['ns'])
    rows = defaultdict(dict)
    arms = []
    for (isa, tier, k, n, op, var, arm), v in cells.items():
        if ops and op not in ops: continue
        if tiers and tier not in tiers: continue
        rows[(isa, tier, op, var, k, n)][arm] = statistics.median(v)
        if arm not in arms: arms.append(arm)
    order = {'l2': 0, 'llc': 1, 'dram': 2}
    print('isa tier op variant k n ' + ' '.join(arms))
    for key in sorted(rows, key=lambda t: (t[0], order.get(t[1], 9), t[2], t[3], t[4], t[5])):
        r = rows[key]
        print(' '.join(map(str, key)) + ' ' + ' '.join('%.2f' % r[a] if a in r else '-' for a in arms))

if __name__ == '__main__':
    args = sys.argv[1:]
    body = 'packed'; ops = None; tiers = None; paths = []
    for a in args:
        if a.startswith('body='): body = a[5:]
        elif a.startswith('ops='): ops = a[4:].split(',')
        elif a.startswith('tiers='): tiers = a[6:].split(',')
        else: paths.append(a)
    main(paths, body, ops, tiers)
