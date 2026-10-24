#!/usr/bin/env python3
"""Pair Bec4096 and Bec256 medians from bench samples: summarise.py samples.json [...]"""
import json
import sys
from collections import defaultdict

PAIRS = [
    ("decode_all/4096/exact", "decode_all/256x16/exact"),
    ("decode_all/4096/padded", "decode_all/256x16/padded"),
    ("decode_all/4096f/exact", "decode_all/256x16/exact"),
    ("decode_all/4096f/padded", "decode_all/256x16/padded"),
    ("block/4096/exact", "block/256/exact"),
    ("block/4096/padded", "block/256/padded"),
    ("block_fresh/4096/padded", "block/256/padded"),
    ("rank/4096/padded", "rank/256/padded"),
    ("counts/4096/padded", "block/256/padded"),
    ("counts_all/4096f/padded", "decode_all/4096f/padded"),
    ("bind/4096/padded", None),
    ("encode/4096", "encode/256x16"),
    ("prepare/4096", "encode/4096"),
    ("emit/4096", "encode/4096"),
    ("head/4096", "encode/4096"),
]

for path in sys.argv[1:]:
    data = json.load(open(path))
    median = {}
    for b in data["benchmarks"]:
        if b.get("aggregate_name") == "median":
            median[b["run_name"]] = b["real_time"]
    families = []
    for name in median:
        family = name.rsplit("/", 1)[1]
        if family not in families:
            families.append(family)
    print(f"# {path}: {data['context'].get('host_name', '')} ns, median")
    header = "| operation | " + " | ".join(families) + " |"
    print(header)
    print("|" + " --- |" * (len(families) + 1))
    for mine, control in PAIRS:
        cells = []
        for f in families:
            a = median.get(f"{mine}/{f}")
            if a is None:
                cells.append("")
                continue
            if control:
                b = median.get(f"{control}/{f}")
                cells.append(f"{a:.1f} / {b:.1f} ({a / b:.2f})" if b else f"{a:.1f}")
            else:
                cells.append(f"{a:.1f}")
        label = mine + (f" vs {control.split('/')[0]}" if control else "")
        print(f"| {label} | " + " | ".join(cells) + " |")
    print()
