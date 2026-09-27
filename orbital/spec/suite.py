#!/usr/bin/env python3
"""Run selected named Orbital checks sequentially; retain every disposition."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from catalog import read_catalog


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tier", choices=("quick", "growth", "scaling", "full", "all"),
                        help="Defaults to quick when running, all when listing")
    parser.add_argument("--manifest", type=Path, help="Case catalog; defaults to cases.json beside this runner")
    parser.add_argument("--case", action="append", default=[], help="Exact name from cases.json; overrides tier")
    parser.add_argument("--list", action="store_true", help="List selected cases without running TLC")
    parser.add_argument("--output", type=Path, help="New directory for this suite; required unless listing")
    parser.add_argument("--timeout", type=float, default=60, help="Seconds per case; stopping is incomplete")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest_path = (args.manifest or root / "cases.json").resolve()
    try:
        available, catalog_sources = read_catalog(manifest_path)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    names = {case["name"] for case in available}
    if set(args.case) - names:
        parser.error("Unknown case: " + ", ".join(sorted(set(args.case) - names)))
    tier = args.tier or ("all" if args.list else "quick")
    cases = [case for case in available if
             (case["name"] in args.case if args.case else tier in ("all", case["tier"]))]
    if args.list:
        print("NAME\tTIER\tMODULE\tCONFIG\tEXPECT\tWITNESS\tWORKERS\tHEAP")
        for case in cases:
            row = {"workers": 1, "heap": "512m", **case}
            print("\t".join(str(row.get(key, "")) for key in
                            ("name", "tier", "module", "config", "expect", "witness", "workers", "heap")))
        return 0
    if args.output is None:
        parser.error("--output is required unless using --list")
    if args.output.resolve().is_relative_to(root):
        parser.error("Keep run output outside the model source directory")
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
            data = catalog_sources[path.resolve()] if path.resolve() in catalog_sources else path.read_bytes()
            destination.write_bytes(data)
            hashes[str(relative)] = hashlib.sha256(data).hexdigest()
    # Also capture external selection catalogs. Preserve their relative include
    # paths and use the bytes loaded above, so a concurrent edit cannot change
    # which case definitions this receipt says selected the frozen checks.
    catalog_base = Path(os.path.commonpath([str(path.parent) for path in catalog_sources]))
    for path, data in catalog_sources.items():
        relative = Path("catalog-inputs") / path.relative_to(catalog_base)
        destination = frozen / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        hashes[str(relative)] = hashlib.sha256(data).hexdigest()
    captured_manifest = Path("catalog-inputs") / manifest_path.relative_to(catalog_base)
    report = {"schema": 1, "selection": cases, "catalog": captured_manifest.as_posix(),
              "source_sha256": hashes, "cases": []}
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
                   "--workers", str(case.get("workers", 1)), "--heap", case.get("heap", "512m")]
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
                       expectation=result["expectation"], purpose=result["purpose"],
                       workers=result["workers"], heap=result["heap"])
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
