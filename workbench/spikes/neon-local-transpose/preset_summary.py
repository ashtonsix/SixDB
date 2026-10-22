#!/usr/bin/env python3
"""Summarize preset.sh runs: median CPU ns of Local and striped per case, and
striped/Local time (below 1 means stripes are faster). Arguments: V2 and V3
preset.json, optionally gzip-compressed."""
import gzip, json, statistics, sys
rows = {}
for label, path in zip(["V2", "V3"], sys.argv[1:]):
    for b in json.load((gzip.open if path.endswith(".gz") else open)(path))["benchmarks"]:
        if b.get("run_type") != "iteration": continue
        rows.setdefault(b["run_name"], {}).setdefault(label, []).append(b["cpu_time"])
cases = sorted({n.rsplit("/", 1)[0] for n in rows}, key=lambda c: (int(c.split("/")[0][1:]), c))
print(f"{'case':14}" + "".join(f"{h:>10}" for h in ["V2 local", "V2 strip", "V2 S/L", "V3 local", "V3 strip", "V3 S/L"]))
for c in cases:
    out = f"{c:14}"
    for m in ["V2", "V3"]:
        l = statistics.median(rows[c + "/local"][m]); s = statistics.median(rows[c + "/striped"][m])
        out += f"{l:10.0f}{s:10.0f}{s / l:10.3f}"
    print(out)
