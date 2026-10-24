#!/usr/bin/env python3
"""Summarise frame3_calibrate: predicted against measured ns per block (points
per point), and whether the predicted cheapest preset matches the measured one."""
import json, sys
from collections import defaultdict

for path in sys.argv[1:]:
    rows = [json.loads(l) for l in open(path) if l.startswith("{")]
    pred = [r for r in rows if "column" in r]
    print(f"== {path}")
    print(f"{'column':16}{'preset':16}{'bits':>6}{'part':>6}  {'sum pred/meas':>15}  {'count pred/meas':>15}  {'point pred/meas':>15}")
    errs = defaultdict(list)
    by_col = defaultdict(dict)
    for r in pred:
        print(f"{r['column']:16}{r['preset']:16}{r['bits_per_value']:6.2f}{r['partial_share']:6.2f}  "
              f"{r['sum_predicted']:7.1f}/{r['sum_measured']:7.1f}  {r['count_predicted']:7.1f}/{r['count_measured']:7.1f}  "
              f"{r['point_predicted']:7.1f}/{r['point_measured']:7.1f}")
        for op in ("sum", "count", "point"):
            m = r[f"{op}_measured"]
            if m > 0:
                errs[op].append(abs(r[f"{op}_predicted"] - m) / m)
            by_col[(r["column"], op)][r["preset"]] = (r[f"{op}_predicted"], m)
    for op, e in errs.items():
        e.sort()
        print(f"{op}: median relative error {e[len(e) // 2]:.2f}, worst {e[-1]:.2f}")
    agree = total = 0
    for (col, op), d in by_col.items():
        if len(d) < 2:
            continue
        p = min(d, key=lambda k: d[k][0])
        m = min(d, key=lambda k: d[k][1])
        # A pick counts as right when its measured time is within 5% of the best.
        total += 1
        agree += d[p][1] <= 1.05 * d[m][1]
    print(f"preset picks within 5% of the measured best: {agree} of {total}")
