#!/usr/bin/env python3
"""Summarise span_compare JSONL: per operation, the geometric mean over
recipe/data columns of each arm's median time relative to BASE, with the best
and worst column. Refuses a column whose image hash differs between arms.

  span_table.py RESULTS.jsonl BASE_ARM [ARM ...]
"""
import collections, json, math, statistics, sys

path, base, *arms = sys.argv[1:]
times, hashes = collections.defaultdict(list), collections.defaultdict(set)
for line in open(path):
    r = json.loads(line)
    times[(r["column"], r["op"], r["arm"])].append(r["ns"])
    hashes[r["column"]].add(r["image_hash"])
bad = [c for c, h in hashes.items() if len(h) > 1]
if bad:
    sys.exit(f"image hashes differ between arms: {bad}")
arms = arms or sorted({k[2] for k in times} - {base})
columns = sorted({k[0] for k in times})
geo = lambda xs: math.exp(sum(map(math.log, xs)) / len(xs))
print(f"| Operation | Columns | {base} ns (geomean) | " +
      " | ".join(f"{a}/{base} (best, worst)" for a in arms) + " |")
print("| --- | ---: | ---: | " + " | ".join("---" for _ in arms) + " |")
for op in sorted({k[1] for k in times}):
    rows = []
    for c in columns:
        m = {a: statistics.median(times[(c, op, a)]) for a in [base, *arms] if times.get((c, op, a))}
        if len(m) == len(arms) + 1:
            rows.append((c, m))
    if not rows:
        continue
    cells = []
    for a in arms:
        r = sorted((m[a] / m[base], c) for c, m in rows)
        cells.append(f"{geo([x for x, _ in r]):.2f} ({r[0][0]:.2f} {r[0][1]}, {r[-1][0]:.2f} {r[-1][1]})")
    print(f"| {op} | {len(rows)} | {geo([m[base] for _, m in rows]):.0f} | " + " | ".join(cells) + " |")
