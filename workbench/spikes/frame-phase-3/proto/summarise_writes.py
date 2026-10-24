#!/usr/bin/env python3
"""Summarise frame3_writes output: per column, workload and policy."""
import json, sys

cols = [("column", 10, "s"), ("workload", 9, "s"), ("policy", 14, "s"), ("reencoded_groups", 6, "d"), ("reencoded_parts", 6, "d"),
        ("ns_per_write", 9, ".1f"), ("declared_store_bytes", 7, ".1f"), ("issued_bytes", 7, ".1f"), ("bits_before", 6, ".2f"),
        ("bits_after", 6, ".2f"), ("sum_before", 6, ".1f"), ("sum_after", 6, ".1f"), ("count_before", 6, ".1f"),
        ("count_after", 6, ".1f"), ("point_before", 6, ".1f"), ("point_after", 6, ".1f"), ("ok", 3, "d")]
heads = ["column", "workload", "policy", "regrp", "repart", "ns/write", "decl B", "iss B", "bits0", "bits1",
         "sum0", "sum1", "cnt0", "cnt1", "pt0", "pt1", "ok"]
for path in sys.argv[1:]:
    print(f"== {path}")
    print(" ".join(f"{h:>{w}}" if f != "s" else f"{h:{w}}" for h, (_, w, f) in zip(heads, cols)))
    for line in open(path):
        r = json.loads(line)
        print(" ".join(f"{r[k]:{w}}" if f == "s" else f"{r[k]:{w}{f}}" for k, w, f in cols))
