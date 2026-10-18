#!/usr/bin/env python3
"""Extract complete K1–64 default-filter tables with source identity and coverage."""
import argparse
import collections
import csv
import hashlib
import json
import math
import pathlib
import statistics

parser = argparse.ArgumentParser()
parser.add_argument("inputs", nargs="+", help="Platform=path/to/runtime.csv")
parser.add_argument("--output", type=pathlib.Path, required=True)
args = parser.parse_args()
ops = ["Equality (==)", "Less (<)", "Less or equal (<=)", "Greater (>)", "Greater or equal (>=)"]
datasets = {}
sources = []
for spec in args.inputs:
    label, filename = spec.split("=", 1)
    assert label not in datasets, label
    path = pathlib.Path(filename)
    values = collections.defaultdict(dict)
    for row in csv.DictReader(path.open()):
        if row["workload"] != "mono" or row["candidate"] != "inline_fused":
            continue
        key = (int(row["width_or_cases"]), int(row["op"]), int(row["active"]), row["metric"])
        rep = int(row["rep"])
        ns = float(row["ns_window"])
        assert rep not in values[key], (label, key, rep)
        assert math.isfinite(ns) and ns > 0
        values[key][rep] = ns
    expected = {(k, op, active, metric) for k in range(1, 65) for op in range(5)
                for active in (65535, 0xa53c) for metric in ("dependent", "independent8")}
    assert values.keys() == expected, (label, expected - values.keys(), values.keys() - expected)
    assert all(set(samples) == set(range(5)) for samples in values.values()), label
    datasets[label] = {key: statistics.median(samples.values()) for key, samples in values.items()}
    sources.append({"profile": label, "source": str(path),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "selected_samples": sum(map(len, values.values()))})

text = ["# SeriesPack filtering — K=1–64", "",
        "Production `inline_fused` consumer after the NEON u32 mask repair, Local geometry,",
        "H=0 (no separated heads), 16 values per operation. Each cell is **dependent",
        "latency / reciprocal throughput**, in **ns per 16-value window**; lower is better",
        "for both. Medians of five repetitions, 100,000 iterations per repetition.", "",
        "Eight throughput recurrences use distinct stream salts. Equal masks no longer",
        "lock the streams to one shared window; occasional address collisions remain.",
        "Query loads, full-result address recurrence and loop control are included.",
        "Serial and throughput are separate measurements. Warm finite working sets",
        "(up to 8 KiB payload plus 512 B queries), not a DRAM or Loom measurement.",
        "These are default filters, not guarded filters or an independent fused baseline.", "",
        "Native Zen 5 and Granite Rapids AVX2/AVX-512, Neoverse V2 NEON. Clang 21.1.8,",
        "O3, explicit SIMD, automatic loop/SLP vectorizers disabled; each host uses its",
        "own compiler tuning. Samples are sequential, with fixed candidate order.",
        "Sparse activity is 0xa53c (8 of 16 lanes); times remain per 16-position window.", "",
        "See [graduation and audit](MASK-GRADUATION.md) for provenance, limits and comparison",
        "with the historical shared-salt table. Adjacent `.csv` and `.sources.json` files",
        "contain numeric medians and source hashes. Raw repetitions remain with the evidence.", ""]
summary = []
for active, activity in ((65535, "Full activity"), (0xa53c, "Sparse activity")):
    for op, operation in enumerate(ops):
        text += [f"## {activity}: {operation}", "", "| K | " + " | ".join(datasets) + " |",
                 "|---:|" + "---:|" * len(datasets)]
        for k in range(1, 65):
            cells = []
            for label, data in datasets.items():
                latency, throughput = (data[k, op, active, metric] for metric in ("dependent", "independent8"))
                cells.append(f"{latency:.2f} / {throughput:.2f}")
                summary.append([label, k, op, active, latency, throughput])
            text.append(f"| {k} | " + " | ".join(cells) + " |")
        text.append("")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(text))
args.output.with_suffix(".sources.json").write_text(json.dumps(sources, indent=2) + "\n")
with args.output.with_suffix(".csv").open("w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["profile", "width", "op", "active", "latency_ns", "reciprocal_throughput_ns"])
    writer.writerows(summary)
print(f"{len(summary)} paired medians, {sum(s['selected_samples'] for s in sources)} samples")
