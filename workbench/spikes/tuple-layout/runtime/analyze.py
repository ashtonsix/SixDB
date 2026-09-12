#!/usr/bin/env python3
"""Offline, unit-aware summary of retained runtime-map measurements."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import median
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from evidence import verify_compact


def summarize(root):
    verify_compact(root)
    samples = sorted(root.glob('*/samples.csv'))
    if not samples:
        raise ValueError(f'No sample CSVs in {root}; use runtime/recover.py '
                         'to restore archived evidence (see evidence/README.md).')
    for path in samples:
        context = json.loads((path.parent / "context.json").read_text())
        values = defaultdict(list)
        with path.open() as source:
            for row in csv.DictReader(source):
                if row["run_type"] != "iteration":
                    continue
                if row.get("error_occurred", "") not in ("", "False", "false", "0"):
                    raise ValueError(f"failed case: {row['name']}")
                scale = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}[row["time_unit"]]
                per_item = float(row["cpu_time"]) * scale / float(row["items_per_iteration"])
                values[row["name"]].append(per_item)
        medians = {name: median(samples) for name, samples in values.items()}
        print(f"\n## {root.name} / {path.parent.name}\n")
        print(f"{len(values)} cases; repetition counts {sorted({len(v) for v in values.values()})}. "
              "Median CPU ns per item. Preparation is one item; invocation is one tuple.\n")
        print(f"Binary: `{context['binary_sha256']['tuple_runtime_bench']}`.\n")
        print("| Case / rows | Read inline | Read bound | Weighted bound | Scalar write | Native write |")
        print("| --- | ---: | ---: | ---: | ---: | ---: |")
        cases = ["bytes64_0", "motif16_0", "motif16_1", "motif16_2",
                 "stress_0_0", "stress_1_0", "stress_2_0", "stress_3_0", "tiny_dense", "tiny_cluster"]
        for rows in (1024, 65536):
            for case in cases:
                cells = []
                for mode in ("read_inline", "read_bound", "weighted_bound", "write_scalar", "write_native"):
                    value = medians.get(f"runtime/{case}/{mode}/{rows}")
                    cells.append("—" if value is None else f"{value:.3f}")
                if all(v == "—" for v in cells):
                    continue
                print(f"| {case} / {rows} | " + " | ".join(cells) + " |")
        for mode in ("prepare_read", "prepare_write"):
            times = [v for k, v in medians.items() if k.endswith("/" + mode)]
            if times:
                print(f"\n{mode}: {min(times):.1f}–{max(times):.1f} ns, across represented cases.")
        print("\nScalar writes coalesce selected bytes. Native writes currently require every physical "
              "byte to be touched; missing entries are unsupported, not zero time. "
              "The scalar64 and scalar8 controls remain in samples; scalar8 does less work. "
              "Read inline still uses runtime controls, not constant specialization.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path, nargs="+")
    args = parser.parse_args()
    print("# Runtime-map measurement summary")
    for root in args.evidence:
        summarize(root)


if __name__ == "__main__":
    main()
