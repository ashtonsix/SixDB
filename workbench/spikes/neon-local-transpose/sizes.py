#!/usr/bin/env python3
"""Text bytes of the measured SeriesPack functions in each variant's benchmark.

Sums nm sizes of symbols instantiated for each width: the dense range reader and
the prepared maintained-replacement bodies. Symbols are grouped by the
`format<K, local>` they mention, so inlined helpers are counted where they land.
"""
import argparse, collections, json, re, subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("--variants", default="original,bit4,current")
parser.add_argument("--output", type=Path)
args = parser.parse_args()
pattern = re.compile(r"ikea::seriespack::format<(\d+)u, \(ikea::seriespack::geometry\)0, 0u>")
sizes = {}
for v in args.variants.split(","):
    out = subprocess.run(["nm", "-S", "-C", "--size-sort", args.build / f"neon_local_transpose_bench_{v}"],
                         capture_output=True, text=True, check=True).stdout
    per = collections.Counter()
    for line in out.splitlines():
        parts = line.split(" ", 3)
        if len(parts) < 4 or parts[2].lower() != "t":
            continue
        widths = set(pattern.findall(parts[3]))
        if len(widths) == 1 and ("dense_range" in parts[3] or "replace" in parts[3]):
            per[int(widths.pop())] += int(parts[1], 16)
    sizes[v] = dict(sorted(per.items()))
widths = sorted({k for v in sizes.values() for k in v})
print("width" + "".join(f"{v:>10}" for v in sizes))
for k in widths:
    print(f"{k:5}" + "".join(f"{sizes[v].get(k, 0):10}" for v in sizes))
if args.output:
    args.output.write_text(json.dumps(sizes, indent=1))
