#!/usr/bin/env python3
"""Run selected named Orbital checks sequentially; retain every disposition."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tier", choices=("quick", "growth", "scaling", "all"), default="quick")
    parser.add_argument("--case", action="append", default=[], help="Exact name from cases.json; overrides tier")
    parser.add_argument("--output", type=Path, required=True, help="New directory for this suite")
    parser.add_argument("--timeout", type=float, default=60, help="Seconds per case; stopping is incomplete")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "cases.json").read_text())
    names = {case["name"] for case in manifest["cases"]}
    if len(names) != len(manifest["cases"]):
        parser.error("Duplicate case name in manifest")
    if set(args.case) - names:
        parser.error("Unknown case: " + ", ".join(sorted(set(args.case) - names)))
    if args.output.resolve().is_relative_to(root):
        parser.error("Keep run output outside the model source directory")
    cases = [case for case in manifest["cases"] if
             (case["name"] in args.case if args.case else args.tier in ("all", case["tier"]))]
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    # Freeze the whole model set once. Individual check.py runs capture this
    # stable directory, so edits to the checkout cannot mix a suite's sources.
    frozen = output / "suite-source"
    frozen.mkdir()
    hashes = {}
    for path in sorted(root.rglob("*")):
        if (path.is_file() and path.suffix in {".tla", ".cfg", ".py", ".json", ".md"}
                and not {"evidence", "__pycache__"}.intersection(path.relative_to(root).parts)):
            relative = path.relative_to(root)
            destination = frozen / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            data = path.read_bytes()
            destination.write_bytes(data)
            hashes[str(relative)] = hashlib.sha256(data).hexdigest()
    report = {"schema": 1, "selection": cases, "source_sha256": hashes, "cases": []}
    receipt = output / "summary.json"
    receipt.write_text(json.dumps(report, indent=2) + "\n")
    failed = False
    for case in cases:
        if hashlib.sha256((root / "check.py").read_bytes()).hexdigest() != hashes["check.py"]:
            raise RuntimeError("Model runner changed during the suite; completed receipts remain available")
        # The checkout runner owns shared tool discovery; it is itself captured
        # and hashed by check.py. Models/configuration come only from frozen.
        command = [sys.executable, str(root / "check.py"),
                   "--module", str(frozen / (case["module"] + ".tla")),
                   "--config", str(frozen / case["config"]),
                   "--output", str(output / "runs"), "--timeout", str(args.timeout),
                   "--workers", str(case.get("workers", 1))]
        if case.get("expect"):
            command += ["--expect", case["expect"]]
        if case.get("witness"):
            command += ["--witness", case["witness"]]
        run = subprocess.run(command, capture_output=True, text=True)
        row = {"name": case["name"], "returncode": run.returncode,
               "stdout": run.stdout, "stderr": run.stderr}
        try:
            answer = json.loads(run.stdout)
            result_path = Path(answer["result"])
            result = json.loads(result_path.read_text())
            row.update(result=result_path.relative_to(output).as_posix(),
                       status=result["status"], states=result["states"], depth=result["depth"],
                       elapsed_seconds=result["elapsed_seconds"], peak_rss_bytes=result["peak_rss_bytes"],
                       expectation=result["expectation"], purpose=result["purpose"])
        except (ValueError, KeyError, OSError):
            row["status"] = "runner_error"
        report["cases"].append(row)
        temporary = receipt.with_suffix(".tmp")
        temporary.write_text(json.dumps(report, indent=2) + "\n")
        temporary.replace(receipt)
        failed |= run.returncode != 0
        print(f"{case['name']}: {row['status']} {row.get('states', {})}", flush=True)
    print(receipt)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
