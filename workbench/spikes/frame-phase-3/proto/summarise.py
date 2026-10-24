#!/usr/bin/env python3
"""Summarise frame3_measure output: medians over process repeats, per column
and arm, with frame arms as ratios to the hand glue on the same preset."""
import json, statistics, sys
from collections import defaultdict

ops = ["sum", "count_ge", "count_eq", "filter", "select", "read", "ns_per_point"]
for path in sys.argv[1:]:
    rows = [json.loads(l) for l in open(path) if l.startswith("{")]
    cells = defaultdict(lambda: defaultdict(list))
    bits = {}
    for r in rows:
        key = (r["isa"], r["column"], r["arm"])
        for op in ops:
            if r[op] >= 0:
                cells[key][op].append(r[op])
        bits[key] = r["bits_per_value"]
    med = {k: {op: statistics.median(v) for op, v in d.items()} for k, d in cells.items()}
    print(f"== {path}")
    isas = sorted({k[0] for k in med})
    for isa in isas:
        cols = sorted({k[1] for k in med if k[0] == isa}, key=lambda c: [k[1] for k in med].index(c))
        print(f"-- {isa}: ns per 256 rows (points: per point); ratio = frame / glue_compact")
        print(f"{'column':16} {'arm':34} {'bits':>6} " + " ".join(f"{op[:8]:>8}" for op in ops))
        for c in cols:
            glue = med.get((isa, c, "glue_compact"), {})
            for arm in [k[2] for k in med if k[0] == isa and k[1] == c]:
                m = med[(isa, c, arm)]
                vals = " ".join(f"{m.get(op, float('nan')):8.2f}" for op in ops)
                print(f"{c:16} {arm:34} {bits[(isa, c, arm)]:6.2f} {vals}")
            fr = med.get((isa, c, "frame_bound_pack_compact"), {})
            if glue and fr:
                print(f"{'':16} {'ratio frame/glue (compact)':34} {'':6} " + " ".join(
                    f"{fr[op] / glue[op]:8.2f}" if op in glue and op in fr and glue[op] > 0 else f"{'':8}" for op in ops))
