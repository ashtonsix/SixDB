#!/usr/bin/env python3
"""Select timing repetitions from a completed run without changing measurements.

Full CSVs and assembly remain in the run bundle. Capacity selection retains the
complete extent sweep for widths 1 and 7, plus every width at the largest extent.
Fixed-payload and bulk comparisons retain every repetition. This is offline
postprocessing: the original source identity and measured files remain intact.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from report import read_and_validate


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    focus = parser.add_mutually_exclusive_group()
    focus.add_argument("--repair-history", action="store_true",
                        help="Keep the 5/7-bit ScanPack repair comparison at the smallest and largest extents")
    focus.add_argument("--bulk-decode", action="store_true",
                       help="Keep selected/prior ScanPack bulk decode repetitions for a decoder-only finding")
    args = parser.parse_args()
    run = args.run
    receipt_path = run / "run.json"
    receipt = json.loads(receipt_path.read_text())
    if receipt["status"] != "complete" or not receipt["source_unchanged"]:
        raise ValueError("Select evidence only from a completed, source-stable run")
    source = run / "timings.csv"
    if digest(source) != receipt["artifact_sha256"]["timings.csv"]:
        raise ValueError("Measured timing CSV changed")
    rows = read_and_validate(source)
    largest = max(int(row["logical_values"]) for row in rows)
    smallest = min(int(row["logical_values"]) for row in rows)
    if args.bulk_decode:
        chosen = [row for row in rows if row["suite"] == "bulk" and row["operation"] == "decode"
                  and row["layout"] == "scan" and row["provider"] in ("selected", "prior")]
        rule = "all selected/prior ScanPack bulk decode repetitions"
    elif args.repair_history:
        chosen = [row for row in rows if int(row["width"]) in (5, 7)
                  and row["layout"] == "scan" and row["provider"] in ("selected", "prior")
                  and int(row["logical_values"]) in (smallest, largest)]
        rule = "ScanPack widths 5 and 7, selected and prior, at minimum and maximum logical extent"
    else:
        chosen = [row for row in rows if row["suite"] != "capacity"
                  or row["capacity_mode"] == "fixed_payload"
                  or int(row["width"]) in (1, 7)
                  or int(row["logical_values"]) == largest]
        rule = "all bulk/fixed-payload rows; capacity widths 1 and 7 at every extent, all widths at maximum logical extent"
    samples = run / "samples.csv"
    with samples.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(chosen)
    read_and_validate(samples)
    selection = {
        "kind": "offline row selection; no new measurement",
        "source": "timings.csv", "source_sha256": digest(source),
        "source_rows": len(rows), "selected_rows": len(chosen),
        "rule": rule,
        "selector_sha256": digest(Path(__file__)),
    }
    (run / "selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    # Capture the postprocessor separately from the original measured sources.
    (run / "selection-script.py").write_bytes(Path(__file__).read_bytes())
    for name in ("samples.csv", "selection.json", "selection-script.py"):
        receipt["artifact_sha256"][name] = digest(run / name)
    receipt["postprocessing"] = selection
    receipt["compact"] = {
        "files": ["samples.csv", "selection.json", "checks.csv", "cache-context.json", "compiler.txt"],
        "regenerate": ["python3", "workbench/spikes/ikea-composition/probes/ikea-integers/report.py", "{evidence}"],
    }
    # Composition and paired-stripe observations are separate, short comparisons.
    for name in ("composition-checks.txt", "composition-timings.csv", "pairs-timings.csv"):
        if (run / name).exists():
            receipt["compact"]["files"].append(name)
    receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(f"Selected {len(chosen)} of {len(rows)} measurements; measured source identity unchanged")


if __name__ == "__main__":
    main()
