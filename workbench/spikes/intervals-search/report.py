#!/usr/bin/env python3
"""Summarise cells.jsonl: per cell, today's time, the best arm and chosen arms.

  report.py RESULTS_DIR [--arms a,b,c] [--stream dependent] [--counts fixed]
"""
import argparse, collections, json, pathlib

p = argparse.ArgumentParser()
p.add_argument("results", nargs="+")
p.add_argument("--arms", default="current,std_branchy,bisect,scan_page,hyb64_page,hyb128_page,hyb256_page")
p.add_argument("--stream")
p.add_argument("--counts")
p.add_argument("--bytes", type=int)
p.add_argument("--exact", action="store_true", help="exclude arms needing a readable tail")
a = p.parse_args()
TAIL = ("scan_tail", "hyb64_fixed", "hyb128_fixed", "hyb256_fixed")
cells = collections.defaultdict(dict)
for r in a.results:
    for f in pathlib.Path(r).rglob("cells.jsonl"):
        for line in f.read_text().splitlines():
            c = json.loads(line)
            key = (c["profile"], c["bytes"], c["counts"], c["stream"], c["n"])
            cells[key][c["arm"]] = c["median"]
arms = a.arms.split(",")
last = None
for key in sorted(cells):
    prof, b, counts, stream, n = key
    if a.stream and stream != a.stream or a.counts and counts != a.counts or a.bytes and b != a.bytes:
        continue
    group = key[:4]
    if group != last:
        print(f"\n{prof} u{8*b} {counts} {stream}")
        print(f"{'n':>6} " + " ".join(f"{x[:12]:>12}" for x in arms) + "  best")
        last = group
    m = cells[key]
    pool = {k: v for k, v in m.items() if not (a.exact and k in TAIL)}
    best = min(pool, key=pool.get)
    row = " ".join(f"{m.get(x, float('nan')):12.2f}" for x in arms)
    print(f"{n:>6} {row}  {best} {pool[best]:.2f} ({m['current'] / pool[best]:.2f}x)")
