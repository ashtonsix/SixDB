#!/usr/bin/env python3
"""Summarize selected native comparisons, preserving raw recovery references."""
import argparse
import collections
import csv
import hashlib
import json
import pathlib
import statistics

p = argparse.ArgumentParser()
p.add_argument("spec", type=pathlib.Path)
p.add_argument("out", type=pathlib.Path)
a = p.parse_args()
spec = json.loads(a.spec.read_text())
a.out.mkdir(parents=True, exist_ok=True)
contexts = []
for study, config in spec.items():
    cells = collections.defaultdict(list)
    traces = collections.defaultdict(set)
    files = []
    dims, extras = config["dimensions"], config.get("extras", [])
    for host, job in config["jobs"].items():
        root = pathlib.Path("build/workers") / job
        status = json.loads((root / "status.json").read_text())
        assert status["state"] == "complete", (job, status["state"])
        job_info = json.loads((root / "job.json").read_text())
        contexts.append({"study": study, "host": host, "job": job, "source": job_info["source"],
            "source_commit": job_info["source_commit"], "script": job_info["script"],
            "machine": {k: job_info["config"].get(k) for k in ("machine", "architecture", "instance_type", "capacity", "env")},
            "raw_artifact": json.loads((root / "artifact.json").read_text()),
            "host_receipt": f"s3://calico-fleet-artifacts/sixdb/workers/{job}/host.json"})
        selected = sorted({file for pattern in config["patterns"] for file in (root / "results").glob(pattern)})
        assert selected, (study, host)
        for file in selected:
            isa = file.name.split("-", 1)[0]
            included = False
            for row in csv.DictReader(file.open()):
                if any(row.get(k) not in values for k, values in config.get("filter", {}).items()):
                    continue
                if not included:
                    files.append({"host": host, "job": job, "file": file.name, "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
                    included = True
                key = (host, isa, *(row[d] for d in dims))
                cells[key].append(row)
                trace_key = (host, isa, *(row[d] for d in dims if d != "implementation"))
                traces[trace_key].add(row["checksum"])
    assert all(len(values) == 1 for values in traces.values()), (study, "consumer traces differ between algorithms")
    fields = ["host", "isa", *dims, "repetitions", "median_ns", "min_ns", "max_ns", *extras]
    with (a.out / (study + ".csv")).open("w") as out:
        w = csv.DictWriter(out, fieldnames=fields); w.writeheader()
        for key, rows in sorted(cells.items()):
            values = [float(r[config.get("timing", "ns")]) for r in rows]
            assert len(values) >= 3, (study, key, len(values))
            assert len({r["checksum"] for r in rows}) == 1, (study, key)
            record = dict(zip(["host", "isa", *dims], key))
            record.update(repetitions=len(values), median_ns=f"{statistics.median(values):.6f}",
                min_ns=f"{min(values):.6f}", max_ns=f"{max(values):.6f}")
            for field in extras:
                # Setup/close vary; explicit median accounting rather than
                # silently using one repetition or mixing them into lookup ns.
                if field in ("setup_ns", "close_ns"):
                    record[field] = f"{statistics.median(float(r[field]) for r in rows):.0f}"
                else:
                    assert len({r[field] for r in rows}) == 1, (study, key, field)
                    record[field] = rows[0][field]
            w.writerow(record)
    (a.out / (study + "-inputs.json")).write_text(json.dumps(files, indent=2) + "\n")
    print(study, len(cells), "cells")
(a.out / "context.json").write_text(json.dumps({
    "scope": "Median/min/max ns per request or value, as named by each metric. Dependent and independent consumers remain separate. Storage counts are explicit logical/allocated operand bytes, not cache-residency evidence. Raw repetitions, exact sources, binaries, codegen, hardware and memory receipts remain in each verified worker archive.",
    "selection": spec, "jobs": contexts}, indent=2) + "\n")
