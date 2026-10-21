#!/usr/bin/env python3
"""Recover verified prior samples/binaries; execute only our captured original driver."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile

archive, target, frozen = map(Path, sys.argv[1:])
expected = "dd117b74e334c5a2f5baa07f23e90051b86a3801580bfc3f1b5ff099ea0e6d7e"
assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected
target.mkdir(parents=True, exist_ok=True)
with tarfile.open(archive) as tar:
    tar.extractall(target, filter="data")
assert (target / "plan.json").is_file()
plan = {p["case"]: p for p in json.loads((target / "plan.json").read_text())}
verified = []
for receipt in sorted((target / "samples").glob("*.json")):
    record = json.loads(receipt.read_text())
    csv = receipt.with_suffix(".csv")
    assert record["returncode"] == 0
    assert hashlib.sha256(csv.read_bytes()).hexdigest() == record["sha256"]
    assert record["argv"] == plan[record["case"]]["argv"]
    verified.append(record["case"])
(target / "recovery.json").write_text(json.dumps({"archive_sha256": expected,
    "verified_complete_cases": verified, "remaining": len(plan) - len(verified),
    "source_archive_sha256": hashlib.sha256((target / "source.tar.gz").read_bytes()).hexdigest()}, indent=2) + "\n")
frozen.mkdir(parents=True, exist_ok=True)
with tarfile.open(target / "source.tar.gz") as tar:
    tar.extractall(frozen, filter="data")
print(f"Recovered {len(verified)}/{len(plan)} verified process cases; {len(plan)-len(verified)} remain", flush=True)
