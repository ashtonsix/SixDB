#!/usr/bin/env python3
"""Frozen experimental spacing/count matrix; separate platform and policy granules."""
import argparse
import csv
import hashlib
import json
import platform
import shutil
import statistics
import subprocess
from pathlib import Path

from run import A, B, B_LARGE


def spaced(low, high, count, quantum, shape):
    """Same endpoints/count/quantum; enforce room for the remaining classes."""
    values = []
    for i in range(count):
        raw = low + (high - low) * i / (count - 1) if shape == "even" else low * (high / low) ** (i / (count - 1))
        value = int(raw / quantum + 0.5) * quantum
        value = max(low if not values else values[-1] + quantum,
                    min(value, high - (count - i - 1) * quantum))
        values.append(value)
    assert values[0] == low and values[-1] == high and len(set(values)) == count
    return tuple(values)


def candidates(page):
    result = [dict(name="current", bits=4, palettes=(A, B), pack=1, route="baseline"),
              dict(name="partition-only", bits=5, palettes=(A, B), pack=1, route="baseline")]
    for count in (16, 32):
        for shape in ("geometric", "even"):
            result.append(dict(name=f"full-{shape}-{count}", bits=4 if count == 16 else 5,
                               palettes=(spaced(16, 3072, count, 16, shape),), pack=0, route="best"))
            result.append(dict(name=f"band-{shape}-{count}", bits=4 if count == 16 else 5,
                               palettes=(A, spaced(256, 3072, count, 64, shape)), pack=1, route="baseline"))
    result.append(dict(name="only-b", bits=4, palettes=(B,), pack=0, route="best"))
    result.append(dict(name="only-b-geometric-15", bits=4,
                       palettes=(spaced(256, 3072, 15, 64, "geometric"),), pack=0, route="best"))
    # The hole-free count is an alternative, not mislabeled as 32 usable classes.
    result.append(dict(name="band-geometric-15", bits=4,
                       palettes=(A, spaced(256, 3072, 15, 64, "geometric")), pack=1, route="baseline"))
    result.append(dict(name="band-geometric-31", bits=5,
                       palettes=(A, spaced(256, 3072, 31, 64, "geometric")), pack=1, route="baseline"))
    result.append(dict(name="selected", bits=4,
                       palettes=(B_LARGE,) if page >= 65536 else (A, B_LARGE if page >= 16384 else B),
                       pack=int(page < 65536), route="best" if page >= 65536 else "baseline"))
    if page > 4096:
        for count in (16, 32):
            for shape in ("geometric", "even"):
                result.append(dict(name=f"wide-{shape}-{count}", bits=4 if count == 16 else 5,
                                   palettes=(A, spaced(256, page * 3 // 4, count, 64, shape)), pack=1,
                                   route="baseline", request_max=page * 3 // 4))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--page", type=int, default=0)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--population", type=int, default=1024)
    parser.add_argument("--rounds", type=int, default=4)
    parser.add_argument("--workloads", default="mixed,tiny,cluster,log,max")
    parser.add_argument("--cases", default="")
    parser.add_argument("--stages", default="all")
    parser.add_argument("--alignment", type=int, default=16)
    parser.add_argument("--selection", choices=("unrestricted", "strict"), default="unrestricted")
    parser.add_argument("--class-lookup", type=int, choices=(0, 1), default=1)
    parser.add_argument("--class-once", type=int, choices=(0, 1), default=1)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--build-context", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    receipt = json.loads(args.build_context.read_text()) if args.build_context else None
    if receipt:
        assert receipt["binary_sha256"] == digest, "binary differs from build receipt"
    owned_binary = args.output / "allocation_palettes_timing"
    shutil.copy2(binary, owned_binary)
    binary = owned_binary.resolve()
    native = subprocess.check_output(["getconf", "PAGESIZE"], text=True).strip()
    page = args.page or int(native)
    cases = candidates(page)
    if args.cases:
        names = set(args.cases.split(","))
        assert names <= {c["name"] for c in cases}, names
        cases = [c for c in cases if c["name"] in names]
    backend = Path(__file__).parent / "backend"
    context = dict(kind=__doc__, page=page, host_page=int(native), host=platform.platform(),
                   backend="frozen palette experiment; independent of production Orbital",
                   backend_origin=json.loads((backend / "snapshot.json").read_text()),
                   binary_sha256=digest, build_context=receipt, cases=cases,
                   options=vars(args) | {"binary":str(binary), "output":str(args.output)},
                   sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in (Path(__file__), Path(__file__).with_name("timing.cpp"),
                                      backend / "finite.cpp", backend / "finite.h", backend / "snapshot.json",
                                      Path("orbital/include/orbital/types.h"))},
                   limits=["Frozen experimental geometry/VM backend, not the current public production allocator",
                           "Controller omits Loom, four ready hints and async scheduling",
                           "Batch-mean percentiles are not individual-operation latency percentiles",
                           "Different OS/CPU/compiler-library results are not pooled"])
    (args.output / "context.json").write_text(json.dumps(context, default=str, indent=2) + "\n")
    all_rows = []
    # Repetitions/cases execute sequentially; each run has fresh owner state.
    for repetition in range(args.repetitions):
        for case in cases:
            for workload in args.workloads.split(","):
                command = [str(binary), "--label", case["name"], "--class-bits", str(case["bits"]),
                           "--page", str(args.page), "--population", str(args.population),
                           "--rounds", str(args.rounds), "--repetition", str(repetition),
                           "--workload", workload, "--alignment", str(args.alignment),
                           "--selection", args.selection, "--routing", case["route"],
                           "--pack-small", str(case["pack"]), "--stage", args.stages,
                           "--class-lookup", str(args.class_lookup), "--class-once", str(args.class_once),
                           "--request-max", str(case.get("request_max", 3072)),
                           "--growth-end", str(case.get("request_max", 3072))]
                if args.cpu is not None:
                    command += ["--cpu", str(args.cpu)]
                for index, table in enumerate(case["palettes"]):
                    command += ["--palette", f"p{index}=" + ",".join(map(str, table))]
                output = subprocess.check_output(command, text=True)
                assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
                rows = list(csv.DictReader(output.splitlines()))
                assert rows and all(int(r["host_page"]) == int(native) for r in rows)
                all_rows.extend(rows)
                with (args.output / "samples.csv").open("w") as f:
                    w = csv.DictWriter(f, fieldnames=list(all_rows[0])); w.writeheader(); w.writerows(all_rows)
                print(f"rep{repetition} {case['name']} {workload}: {len(rows)} stages", flush=True)
    keys = ("candidate", "workload", "stage", "host_page", "policy_page", "class_bits", "alignment")
    groups = {}
    for row in all_rows:
        groups.setdefault(tuple(row[k] for k in keys), []).append(row)
    summaries = []
    for key, rows in groups.items():
        row = dict(zip(keys, key)) | {"repetitions":len(rows)}
        for metric in ("mean_ns", "batch_p50_ns", "batch_p99_ns", "maintenance_ns", "backed_bytes",
                       "reserved_bytes", "metadata_bytes", "capacity_bytes", "requested_bytes",
                       "examined_groups", "page_events", "copied_bytes"):
            row[metric] = statistics.median(float(r[metric]) for r in rows)
        summaries.append(row)
    with (args.output / "summary.csv").open("w") as f:
        w = csv.DictWriter(f, fieldnames=list(summaries[0])); w.writeheader(); w.writerows(summaries)
    print(f"{len(all_rows)} samples; {len(summaries)} within-host medians: {args.output}")


if __name__ == "__main__":
    main()
