#!/usr/bin/env python3
"""Keep all cell medians and source references; raw repetitions stay recoverable."""
import argparse
import collections
import csv
import json
import pathlib
import statistics

p = argparse.ArgumentParser()
p.add_argument("--job", action="append", nargs=2, metavar=("HOST", "JOB"), required=True)
p.add_argument("--out", type=pathlib.Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
dimensions = ["host", "isa", "layout", "maximum", "count", "nodes", "range_span", "implementation", "metric"]
cells = collections.defaultdict(list)
traces = collections.defaultdict(set)
context = []
for host, job in a.job:
    root = pathlib.Path("build/workers") / job
    job_info = json.loads((root / "job.json").read_text())
    context.append({"host": host, "job": job, "source": job_info["source"],
                    "source_commit": job_info["source_commit"], "script": job_info["script"],
                    "machine": {k: job_info["config"].get(k) for k in ("machine", "architecture", "instance_type", "capacity", "env")},
                    "artifact": json.loads((root / "artifact.json").read_text()),
                    "archive": f"s3://calico-fleet-artifacts/sixdb/workers/{job}"})
    for file in sorted((root / "results").glob("*-m*-n*-p*.csv")):
        isa = file.name.split("-", 1)[0]
        for row in csv.DictReader(file.open()):
            assert all(row.get(field) is not None for field in dimensions[2:]), file
            row.update(host=host, isa=isa)
            key = tuple(row[d] for d in dimensions)
            cells[key].append(row)
            traces[key[:7] + key[8:]].add(row["checksum"])
assert all(len(values) == 1 for values in traces.values()), "Consumer traces differ between algorithms"
fields = dimensions + ["repetitions", "median_ns", "min_ns", "max_ns", "metadata_bytes", "child_bytes", "shared_payload_bytes"]
with (a.out / "medians.csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    for key, rows in sorted(cells.items()):
        values = [float(r["ns"]) for r in rows]
        assert len(values) >= 3, (key, len(values))
        result = dict(zip(dimensions, key))
        result.update(repetitions=len(values), median_ns=f"{statistics.median(values):.6f}",
                      min_ns=f"{min(values):.6f}", max_ns=f"{max(values):.6f}")
        for d in fields[-3:]:
            assert len({r[d] for r in rows}) == 1
            result[d] = rows[0][d]
        writer.writerow(result)
(a.out / "context.json").write_text(json.dumps({"scope": "Read-only search plus query preparation and selected 64-byte child descriptor consumption. Logical allocation bytes are not measured RSS. Names retain scalar/hybrid/full-scan distinctions. All medians retained, including losses; source/host/binary receipts and raw repetitions remain in each verified worker archive.", "jobs": context}, indent=2) + "\n")
print(f"Retained {len(cells)} cells from {len(a.job)} native jobs")
