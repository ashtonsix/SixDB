#!/usr/bin/env python3
"""One arm/case per process; keep all phase rows, binary hashes and argv receipts."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import random
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument("binaries", type=Path, help="directory containing values-{2,8,16}")
p.add_argument("output", type=Path)
p.add_argument("--cpu", required=True)
p.add_argument("--reps", type=int, default=9)
p.add_argument("--bag-policies", action="store_true", help="coupled inline/bucket/storage controls, including mixed fanout")
p.add_argument("--widths", type=int, nargs='+', default=[2,8,16])
p.add_argument("--passes", type=int, default=1)
a = p.parse_args()
assert a.reps > 0
assert a.passes > 0 and (a.bag_policies or a.passes == 1)
a.output.mkdir(parents=True, exist_ok=True)
fanouts = {2: [1, 2, 4, 5, 8, 9, 16, 32], 8: [1, 2, 3, 4, 8, 32], 16: [1, 2, 3, 8, 32]}
if a.bag_policies:
    fanouts = {2: [0,1,4,5,8,9,12,13,16,17,32], 8: [0,1,2,3,4,5,8,32], 16: [0,1,2,3,8,32]}
assert len(set(a.widths)) == len(a.widths) and set(a.widths) <= fanouts.keys()
fanouts = {w: fanouts[w] for w in a.widths}
binaries = {w: (a.binaries / f"values-{w}").resolve() for w in fanouts}
receipt = {"binary_sha256": {w: hashlib.sha256(path.read_bytes()).hexdigest() for w, path in binaries.items()}, "processes": []}
schedule = [(w, k, arm, fanout, large) for w, fans in fanouts.items() for k in ((2,) if a.bag_policies else (1, 2))
            for arm in range(10 if a.bag_policies else 5) for fanout in ([1] if k == 1 else fans) for large in (0, 1)]
random.Random(912).shuffle(schedule)
base_schedule = schedule
schedule = [(pass_index, *case) for pass_index in range(a.passes)
            for case in (base_schedule if pass_index%2 == 0 else reversed(base_schedule))]
if a.bag_policies:
    receipt.update(bag_policies=True, widths=a.widths, passes=a.passes, reps=a.reps)
with (a.output / "values.csv").open("w") as stream:
    writer = None
    for order, (pass_index, w, k, arm, fanout, large) in enumerate(schedule):
        cmd = ["taskset", "-c", a.cpu, str(binaries[w]), str(k), str(arm), str(fanout), str(large), str(a.reps)]
        started = time.time()
        result = subprocess.run(cmd, capture_output=True, text=True)
        receipt["processes"].append({"argv": cmd, "started_unix": started, "ended_unix": time.time(),
                                     "returncode": result.returncode, "stderr": result.stderr})
        (a.output / "processes.json").write_text(json.dumps(receipt, indent=2))
        if result.returncode:
            raise RuntimeError((cmd, result.stderr, result.stdout))
        reader = csv.DictReader(io.StringIO(result.stdout))
        rows = list(reader)
        assert len(rows) == 9 * a.reps
        if a.bag_policies:
            for row in rows:row.update(pass_index=pass_index, process_order=order)
        fields = list(rows[0])
        if writer is None:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
        else:
            assert writer.fieldnames == fields
        writer.writerows(rows)
        stream.flush()
        print(f"{order+1}/{len(schedule)} W{w} kind{k} arm{arm} fanout{fanout} large{large}", flush=True)
