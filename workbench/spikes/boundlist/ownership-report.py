#!/usr/bin/env python3
"""Pair full frozen/current consumers and summarize native codec experiments."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
import statistics

p = argparse.ArgumentParser()
p.add_argument("group", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--ownership-group", type=Path, help="Final ownership cohort when encoding used an earlier identical codec capture")
p.add_argument("--partial", action="store_true", help="Allow absent profiles while workers finish")
args = p.parse_args()
receipt = json.loads(args.group.read_text())
ownership_receipt = json.loads((args.ownership_group or args.group).read_text())
assert receipt["members"].keys() == ownership_receipt["members"].keys()
args.output.mkdir(parents=True, exist_ok=True)


def write(name, rows):
    if not rows:
        return
    with (args.output / name).open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def result_root(member, group):
    job = group["members"][member]
    # The group schema may wrap the job identity in its recorded submission.
    job_id = job.get("job_id") or job.get("job")
    if isinstance(job_id, dict):
        job_id = job_id.get("id") or job_id.get("job_id")
    if not job_id:
        raise ValueError((member, job))
    return Path("build/workers") / job_id / "results"


ownership, encoding = [], []
for member in receipt["members"]:
    root = result_root(member, ownership_receipt)
    encoding_root = result_root(member, receipt)
    for profile in ("neon", "avx2", "avx512"):
        host = member if profile == "neon" else f"{member}-{profile}"
        for kind, dimensions in {
            "reads": ("layout", "maximum", "rows", "used", "tombstones", "fixed", "placement", "metric"),
            "packets": ("layout", "maximum", "nodes", "used", "placement", "consumer", "chains"),
            "mutation": ("layout", "maximum", "nodes", "used", "metric", "chains"),
            "packing": ("layout", "maximum", "used", "placement", "shrink", "blocks"),
        }.items():
            cells = defaultdict(lambda: defaultdict(list))
            checksums = defaultdict(dict)
            footprints = defaultdict(dict)
            memory_fields = ("metadata_bytes", "payload_bytes", "payload_capacity_bytes", "binding_bytes",
                             "scratch_bytes", "journal_bytes", "layout_bytes")
            for path in sorted(root.glob(f"{profile}-*-{kind}*.csv")):
                variant = "before" if "-before-" in path.name else "after"
                for r in csv.DictReader(path.open()):
                    if kind in ("reads", "mutation", "packing") and r["implementation"] != ("current_streampack" if kind == "reads" else "production"):
                        continue
                    key = tuple(r[x] for x in dimensions)
                    cells[key][variant].append(float(r.get("ns", r.get("ns_per_block"))))
                    if key in checksums and variant in checksums[key]:
                        assert checksums[key][variant] == r["checksum"], (path, key)
                    checksums[key][variant] = r["checksum"]
                    footprints[key][variant] = {field: r.get(field, "") for field in memory_fields}
            for key, samples in cells.items():
                assert set(samples) == {"before", "after"}, (host, kind, key, samples)
                assert all(len(samples[x]) == 5 for x in ("before", "after")), (host, kind, key, samples)
                assert checksums[key]["before"] == checksums[key]["after"], (host, kind, key)
                assert footprints[key]["before"] == footprints[key]["after"], (host, kind, key, footprints[key])
                a, b = (statistics.median(samples[x]) for x in ("before", "after"))
                ownership.append({"host": host, "kind": kind, "case": json.dumps(dict(zip(dimensions, key)), separators=(",", ":")),
                    "before_ns": a, "after_ns": b, "delta_ns": b-a, "ratio": b/a,
                    "checksum": checksums[key]["after"],
                    "before_min": min(samples["before"]), "before_max": max(samples["before"]),
                    "after_min": min(samples["after"]), "after_max": max(samples["after"]),
                    "before_reps": len(samples["before"]), "after_reps": len(samples["after"]),
                    "before_samples_ns": json.dumps(samples["before"], separators=(",", ":")),
                    "after_samples_ns": json.dumps(samples["after"], separators=(",", ":")),
                    **footprints[key]["after"]})
        cells = defaultdict(list)
        dimensions = ("format", "maximum", "count", "nodes", "operation", "chains", "encoded_bytes", "geometry_backing_bytes")
        checksums = {}
        cell_checksums = {}
        for path in sorted(encoding_root.glob(f"{profile}-encoding-*.csv")):
            for r in csv.DictReader(path.open()):
                if "resident_geometry_bytes" in r:
                    # Initial raw label counted touched backing, never measured
                    # per-arm RSS or established a hardware cache residence.
                    r["geometry_backing_bytes"] = r.pop("resident_geometry_bytes")
                key = tuple(r[x] for x in dimensions)
                cells[key].append(float(r["ns"]))
                assert key not in cell_checksums or cell_checksums[key] == r["checksum"], (path, key)
                cell_checksums[key] = r["checksum"]
                logical = tuple(r[x] for x in ("maximum", "count", "nodes", "operation", "chains", "rep"))
                assert logical not in checksums or checksums[logical] == r["checksum"], (path, logical, r["format"])
                checksums[logical] = r["checksum"]
        for key, samples in cells.items():
            encoding.append({"host": host, **dict(zip(dimensions, key)), "ns": statistics.median(samples),
                             "min_ns": min(samples), "max_ns": max(samples), "reps": len(samples),
                             "checksum": cell_checksums[key],
                             "samples_ns": json.dumps(samples, separators=(",", ":"))})
expected = {member if member == "neon" else f"{member}-{profile}"
            for member in receipt["members"] for profile in (("neon",) if member == "neon" else ("avx2", "avx512"))}
if not args.partial:
    assert {r["host"] for r in ownership} == expected
    assert {r["host"] for r in encoding} == expected
    for host in expected:
        for kind, count in {"reads":320,"packets":192,"mutation":32,"packing":64}.items():
            assert sum(r["host"] == host and r["kind"] == kind for r in ownership) == count, (host,kind)
        assert sum(r["host"] == host for r in encoding) == 3280, host
        assert all(r["reps"] == 6 for r in encoding if r["host"] == host), host
write("ownership.csv", ownership)
write("encoding.csv", encoding)
print("ownership", len(ownership), "encoding", len(encoding))
for host in sorted({r["host"] for r in ownership}):
    for kind in ("reads", "packets", "mutation", "packing"):
        rows = [r for r in ownership if r["host"] == host and r["kind"] == kind]
        if rows:
            print(host, kind, len(rows), "median ratio", round(statistics.median(r["ratio"] for r in rows), 4),
                  "median delta ns", round(statistics.median(r["delta_ns"] for r in rows), 4),
                  "range", round(min(r["ratio"] for r in rows), 4), round(max(r["ratio"] for r in rows), 4))
