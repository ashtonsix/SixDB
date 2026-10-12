#!/usr/bin/env python3
"""Retain comparable samples and absolute counters from a composition worker."""
import csv
import json
from pathlib import Path
import sys

source, destination = map(Path, sys.argv[1:])
destination.mkdir(parents=True, exist_ok=True)

def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))

def save(name, items):
    if not items:
        return
    keys = list(items[0])
    with (destination / name).open("w", newline="") as stream:
        out = csv.DictWriter(stream, keys)
        out.writeheader()
        out.writerows(items)

ring, mutable, lifecycle, phases = [], [], [], []
for prefix in ("ring", "hint"):
    for path in sorted(source.glob(prefix + "-*.csv")):
        trial = path.name.split("-")[1]
        binary = path.name.split("-")[2] if prefix == "hint" else "streampack_ring"
        for row in rows(path):
            ring.append(dict(trial=trial, build=binary, **row))
for path in sorted(source.glob("mutation-*.csv")):
    trial, binary = path.name.split("-")[1:3]
    for row in rows(path):
        value = dict(trial=trial, build=binary, **row)
        sample = 16 if "selected-1of16" in row["operation"] else 1
        value["ns_per_selected_assigned_rank"] = float(row["ns_per_input_row"]) * sample
        commands = int(row["commands"])
        value["ns_per_command"] = (float(row["ns_per_input_row"]) * int(row["rows"]) / commands) if commands else ""
        mutable.append(value)
for prefix in ("mvcc", "single", "prepared"):
    for path in sorted(source.glob(prefix + "-*.csv")):
        fields = path.stem.split("-")
        binary = "streampack_lifecycle"
        if prefix == "prepared":
            _, trial, binary, recipe, publication = fields[:5]
            capacity, batch, passes = 256, 16, 4
            objects = int(fields[5]) if len(fields) == 6 else 256
        elif prefix == "mvcc":
            _, trial, recipe, objects, publication = fields
            capacity, batch, passes = 256, 16, 4
            objects = int(objects)
        else:
            _, trial, recipe, capacity = fields
            capacity, batch, passes = int(capacity), 1, 4
            objects, publication = 1048576 // capacity, "damage"
        last = rows(path)[-1]
        edits = int(last["edits"])
        projected = objects * capacity * int(last["epoch"]) * passes
        lifecycle.append(dict(case=prefix, trial=trial, build=binary, recipe=recipe, objects=objects,
            capacity=capacity, batch=batch, publication=publication, **last,
            edit_ns_per_edit=float(last["edit_ns"]) / edits,
            update_ns_per_edit=sum(float(last[k]) for k in ("edit_ns", "rewrite_ns", "admission_ns", "maintenance_ns")) / edits,
            projection_ns_per_row=float(last["traversal_ns"]) / projected,
            non_oracle_wall_ns=float(last["wall_ns"]) - float(last["oracle_ns"])))
for path in sorted(source.glob("phases-*.txt")):
    _, recipe, publication = path.stem.split("-")
    values = {}
    for line in path.read_text().splitlines():
        if line.startswith("phase_") and "=" in line and " " not in line:
            key, value = line.split("=")
            values[key] = float(value)
    last = rows(path.with_suffix(".csv"))[-1]
    phases.append(dict(recipe=recipe, publication=publication, edits=int(last["edits"]),
        edit_ns=float(last["edit_ns"]), **values,
        unclassified_edit_ns=float(last["edit_ns"]) - sum(values.values())))
# Visibility changes must preserve exact command/record counts, not just bytes.
counts = {}
for row in mutable:
    if not int(row["commands"]):
        continue
    key = tuple(row[k] for k in ("trial", "method", "population", "rows", "capacity", "operation", "repetition"))
    value = (row["commands"], row["damage_records"])
    if key in counts:
        assert counts[key] == value, (key, counts[key], value)
    counts[key] = value
save("ring-samples.csv", ring)
save("mutable-samples.csv", mutable)
save("lifecycle-samples.csv", lifecycle)
save("phase-samples.csv", phases)
(destination / "selection.json").write_text(json.dumps({
    "scope": "All completed composition comparison samples, both process orders and losses; full worker archive owns binaries, logs and mappings.",
    "counts": {"ring": len(ring), "mutable": len(mutable), "lifecycle": len(lifecycle), "phase": len(phases)},
    "limits": ["MVCC phase clocks are separate diagnostic runs and leave an unclassified edit remainder.",
        "Projection selected rank includes empty and dead selections; ns/input-row multiplied by 16 for the 1of16 query.",
        "Same-reservation command timing edits only one in four input rows.",
        "Ring traces have equal-length chains, no irregular refill, and consume lengths plus tail4, not complete payload bandwidth."]
}, indent=2) + "\n")
print(json.dumps({"ring": len(ring), "mutable": len(mutable), "lifecycle": len(lifecycle), "phase": len(phases)}))
