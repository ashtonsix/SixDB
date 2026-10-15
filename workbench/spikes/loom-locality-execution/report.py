#!/usr/bin/env python3
"""Qualify complete consumer cells and retain policy costs, including regressions."""
import argparse
import csv
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

SCALES = ("hot", "large")
WINDOW_MODES = ("reference", "contiguous", "merged", "busy")
METHODS = ("direct", "manual", "manual_hints", "ring", "ring_hints")
PHASES = ("reused", "interference", "changing", "return", "generation", "domain", "expired")
EXECUTION_MODES = ("direct", "ring", "disabled", "sampling_only", "adaptive", "caller_known")
CUTOFFS = (32, 64, 128)
COST_CASES = {"known_direct": 8, "cheap": 5, "expensive": 7, "expired": 7}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(path):
    with path.open(newline="") as stream:
        result = list(csv.DictReader(stream))
    require(bool(result), f"empty CSV: {path.name}")
    return result


def summarize(group):
    values = [float(r["ns_per_request"] if "ns_per_request" in r else r["ns_per_operation"]) for r in group]
    require(all(math.isfinite(v) and v > 0 for v in values), "nonpositive/nonfinite timing")
    return dict(median=statistics.median(values), min=min(values), max=max(values), repetitions=len(values))


def expected_window_cells():
    result = set()
    for scale in SCALES:
        for kind in range(3):
            owners = ("generic", "word") if kind < 2 else ("generic",)
            arms = [(owner, method) for owner in owners for method in METHODS]
            arms += [("abseil", "direct"), ("abseil", "prefetch")]
            for mode in WINDOW_MODES:
                for mixed in (0, 1):
                    for concurrency in ((1, 32) if scale == "hot" else (32,)):
                        for owner, method in arms:
                            result.add((scale, kind, mode, mixed, concurrency, owner, method))
    return result


def expected_execution_cells():
    return {(scale, kind, mixed, phase, mode, cutoff)
            for scale in SCALES for kind in range(3) for mixed in (0, 1)
            for phase in PHASES for mode in EXECUTION_MODES for cutoff in CUTOFFS}


def exact_cells(actual, expected, label, partial):
    extra = set(actual) - expected
    missing = expected - set(actual)
    require(not extra, f"unexpected {label} cells: {sorted(extra)[:8]}")
    require(partial or not missing, f"missing {len(missing)} {label} cells: {sorted(missing)[:8]}")


def repetitions(group, count, label, partial):
    actual = [int(r["rep"]) for r in group]
    require(len(actual) == len(set(actual)), f"duplicate repetitions: {label}")
    require(set(actual) <= set(range(count)), f"out-of-range repetitions: {label}: {actual}")
    require(partial or set(actual) == set(range(count)), f"missing repetitions: {label}: {sorted(actual)}")


def same_inventory(inventory, key, row, fields):
    values = tuple(row[field] for field in fields)
    if key in inventory:
        require(values == inventory[key], f"changed logical work/storage: {key}: {values} != {inventory[key]}")
    else:
        inventory[key] = values


def keys_for(scale, kind):
    return 6144 if scale == "hot" else 14400000 if kind == 0 else 3600000


def verify_receipts(root, source_root, hashes):
    window_targets = {f"loom_window_{scale}_{kind}_{mode}"
                      for scale in SCALES for kind in range(3) for mode in WINDOW_MODES}
    execution_targets = {f"loom_execution_{scale}_{kind}" for scale in SCALES for kind in range(3)}
    check_targets = {"loom_contiguous_check", "loom_contiguous_identity_check", "loom_locality_check"}
    targets = window_targets | execution_targets | check_targets | {"loom_policy_cost"}
    manifest_path = root / "binaries.json"
    manifest = json.loads(manifest_path.read_text())
    require(set(manifest) == targets, f"binary inventory differs: missing={sorted(targets - set(manifest))}, extra={sorted(set(manifest) - targets)}")
    for target in sorted(targets):
        path = root / target
        require(path.is_file(), f"missing retained binary: {target}")
        require(digest(path) == manifest[target], f"binary hash mismatch: {target}")
    hashes[manifest_path.name] = digest(manifest_path)

    checks_path = root / "check.txt"
    checks = checks_path.read_text()
    expected_checks = check_targets | {target + "_check" for target in window_targets | execution_targets}
    passed = re.findall(r"Test\s+#\d+:\s+(\S+)\s+\.{2,}\s+Passed\b", checks)
    require(len(passed) == len(expected_checks) and set(passed) == expected_checks,
            f"CTest identities differ: expected {len(expected_checks)}, got {len(passed)}")
    require(re.search(rf"100% tests passed, 0 tests failed out of {len(expected_checks)}\b", checks) is not None,
            "missing complete successful CTest summary")
    hashes[checks_path.name] = digest(checks_path)

    # Execute current controlled analysis tools over retained data, never a
    # captured binary or an untrusted script from a worker archive.
    here = Path(__file__).resolve().parent
    checker = here / "check_consumer_codegen.py"
    environment = dict(os.environ, PYTHONOPTIMIZE="0")
    for target in sorted(window_targets):
        report_path = root / (target + "-codegen.json")
        assembly = root / (target + ".asm.gz")
        receipt = json.loads(report_path.read_text())
        require(assembly.is_file(), f"missing codegen assembly: {target}")
        command = [sys.executable, "-B", str(checker), str(assembly)]
        if int(target.split("_")[3]) < 2:
            command.append("--word-control")
        replay = subprocess.run(command, capture_output=True, text=True, env=environment)
        require(replay.returncode == 0, f"codegen replay failed: {target}: {replay.stderr[-2000:]}")
        require(json.loads(replay.stdout) == receipt, f"codegen receipt differs from retained assembly: {target}")
        hashes[report_path.name] = digest(report_path)
        hashes[assembly.name] = digest(assembly)

    receipt_path = root / "consumer.receipt.json"
    receipt = json.loads(receipt_path.read_text())
    for field in ("source_sha256", "derived_sha256"):
        require(re.fullmatch(r"[0-9a-f]{64}", receipt.get(field, "")) is not None, f"invalid consumer {field}")
    source_verified = False
    derived_verified = False
    if source_root is not None:
        source = source_root / "workbench/spikes/hashtable-design/staged.cpp"
        require(digest(source) == receipt["source_sha256"], "consumer source hash differs from supplied source root")
        with tempfile.TemporaryDirectory(prefix="loom-consumer-verify-") as temporary:
            derived = Path(temporary) / "consumer.cpp"
            replay = subprocess.run([sys.executable, "-B", str(here / "prepare_consumer.py"), str(source), str(derived)],
                                    capture_output=True, text=True, env=environment)
            require(replay.returncode == 0, f"controlled consumer derivation failed: {replay.stderr[-2000:]}")
            require(digest(derived) == receipt["derived_sha256"], "consumer derived hash does not reproduce")
        source_verified = derived_verified = True
    hashes[receipt_path.name] = digest(receipt_path)
    return dict(checks=len(expected_checks), binary_hashes=len(targets), codegen_replays=len(window_targets),
                consumer_source_verified=source_verified, consumer_derived_verified=derived_verified,
                consumer_source_sha256=receipt["source_sha256"], consumer_derived_sha256=receipt["derived_sha256"],
                tools={path.name: digest(path) for path in
                       (Path(__file__), here / "prepare_consumer.py", checker)})


def report(args):
    root = args.root
    windows = defaultdict(list)
    phase = defaultdict(list)
    cost = defaultdict(list)
    inventory = {}
    hashes = {}
    for path in sorted(root.glob("loom_window_*.csv")):
        match = re.fullmatch(r"loom_window_(hot|large)_([0-2])_(reference|contiguous|merged|busy)", path.stem)
        require(match is not None, f"unexpected window CSV: {path.name}")
        scale, kind, mode = match.groups()
        kind = int(kind)
        for row in rows(path):
            mixed = int(row["mixed"])
            concurrency = int(row["concurrency"])
            require(int(row["kind"]) == kind and mixed in (0, 1), f"window kind/mixed mismatch: {path.name}")
            require(int(row["K"]) == 40 and int(row["W"]) == (0 if kind == 0 else 8), f"window key/value shape: {path.name}")
            geometry = 9 if scale == "hot" and kind == 0 else 10 if scale == "hot" else 20 if kind == 0 else 19
            require(int(row["A"]) == geometry and int(row["B"]) == geometry - 4 and
                    int(row["unit"]) == (64 if kind == 0 else 128 if kind == 1 else 192), f"window geometry: {path.name}")
            keys = keys_for(scale, kind)
            graph = keys * (mixed + 1)
            requests = graph * max(1, (262144 + graph - 1) // graph)
            require(int(row["keys"]) == keys and int(row["requests"]) == requests and int(row["graph_hints"]) == 1,
                    f"window request graph differs: {path.name}")
            key = (scale, kind, mode, mixed, concurrency, row["owner"], row["method"])
            windows[key].append(row)
            # Backing may vary with allocator reuse; raw receipts retain it.
            same_inventory(inventory, ("window", scale, kind, mixed, concurrency, row["owner"], row["method"]), row,
                           ("requested", "metadata", "jobs_bytes", "graph_bytes", "ring_bytes", "ring_visits", "graph_hints", "requests"))
        hashes[path.name] = digest(path)

    for path in sorted(root.glob("loom_execution_*.csv")):
        match = re.fullmatch(r"loom_execution_(hot|large)_([0-2])", path.stem)
        require(match is not None, f"unexpected execution CSV: {path.name}")
        scale, kind = match.groups()
        kind = int(kind)
        for row in rows(path):
            mixed = int(row["mixed"])
            mode = row["mode"]
            require(int(row["kind"]) == kind and mixed in (0, 1) and int(row["keys"]) == keys_for(scale, kind),
                    f"execution owner shape: {path.name}")
            direct, ring = int(row["direct_batches"]), int(row["ring_batches"])
            sample_requests, sample_ns = int(row["sample_requests"]), int(row["sample_ns"])
            require(direct >= 0 and ring >= 0 and direct + ring == args.iterations and
                    int(row["requests"]) == args.iterations * 256, f"execution useful work: {path.name}")
            require(0 <= sample_requests <= int(row["requests"]) and sample_requests % 256 == 0 and
                    ((sample_requests == 0 and sample_ns == 0) or (sample_requests > 0 and sample_ns > 0)),
                    f"execution sampling work: {path.name}")
            require(int(row["record_bytes"]) == 56, f"execution record shape: {path.name}")
            require(int(row["ring_visits"]) >= 0 and bool(int(row["ring_visits"])) == bool(ring), f"execution Ring inventory: {path.name}")
            if mode in ("direct", "ring", "disabled", "caller_known"):
                require(sample_requests == 0, f"disabled control sampled work: {path.name}")
            else:
                require(sample_requests > 0, f"sampled control omitted actual samples: {path.name}")
            if mode in ("direct", "caller_known"):
                require(direct == args.iterations and int(row["decision_calls"]) == 0, f"fixed direct path: {path.name}")
            elif mode == "ring":
                require(ring == args.iterations and int(row["decision_calls"]) == 0, f"fixed Ring path: {path.name}")
            else:
                require(int(row["decision_calls"]) == args.iterations, f"ordinary policy call inventory: {path.name}")
                if mode in ("disabled", "sampling_only"):
                    require(ring == args.iterations, f"Ring control changed path: {path.name}")
            key = (scale, kind, mixed, row["phase"], mode, int(row["cutoff"]))
            phase[key].append(row)
            same_inventory(inventory, ("execution", scale, kind, mixed, row["phase"]), row,
                           ("requests", "checksum", "record_bytes", "trace_bytes", "graph_bytes", "interference_bytes"))
        hashes[path.name] = digest(path)

    cost_path = root / "policy-cost.csv"
    if cost_path.exists():
        for row in rows(cost_path):
            case = row["case"]
            require(case in COST_CASES, f"unexpected policy-cost case: {case}")
            require(int(row["operations"]) == 1000000 and int(row["record_bytes"]) == 56 and
                    int(row["checksum"]) == COST_CASES[case] * 1000000, f"policy-cost useful outcome: {case}")
            cost[case].append(row)
        hashes[cost_path.name] = digest(cost_path)

    exact_cells(windows, expected_window_cells(), "window", args.partial)
    exact_cells(phase, expected_execution_cells(), "execution", args.partial)
    exact_cells(cost, set(COST_CASES), "policy-cost", args.partial)
    for label, groups in (("window", windows), ("execution", phase), ("policy-cost", cost)):
        for key, group in groups.items():
            repetitions(group, args.repetitions, f"{label} {key}", args.partial)

    qualification = dict(complete_matrix=not args.partial, repetitions=args.repetitions,
                         execution_iterations=args.iterations, receipts_verified=False)
    if args.qualify:
        qualification |= verify_receipts(root, args.source_root, hashes)
        qualification["receipts_verified"] = True
    window_summary = [dict(zip(("scale", "kind", "mode", "mixed", "concurrency", "owner", "method"), key)) | summarize(group)
                      for key, group in sorted(windows.items())]
    phase_summary = [dict(zip(("scale", "kind", "mixed", "phase", "mode", "cutoff"), key)) | summarize(group) |
                     {field: [int(row[field]) for row in sorted(group, key=lambda row: int(row["rep"]))]
                      for field in ("sample_requests", "sample_ns", "direct_batches", "ring_batches", "decision_calls")}
                     for key, group in sorted(phase.items())]
    out = dict(window=window_summary, execution=phase_summary, policy_cost={key: summarize(group) for key, group in cost.items()},
               input_hashes=hashes, qualification=qualification, limits=[
                   "Window controls use full graphs; execution controls use charged complete-query prefixes.",
                   "Source/geometry match does not establish temporal pairing: window mode processes run in fixed per-kind order.",
                   "Hint procedures differ in volume: tail hints may duplicate a cache line and contiguous coverage caps at eight lines.",
                   "Actual emitted-size inventory is not retained, so whether the cap lowered a workload window remains unproven.",
                   "Window storage equality covers the original owner/job inventory; the candidate's 1025-entry prepared shape table is additional prototype stack storage.",
                   "Sampling is extra work and does not advance the useful request denominator.",
                   "Policy predicts cheap serial cost, never cache residency.",
                   "Generation/domain/expiry phases are fixture identity controls; no real migration.",
                   "The 128 MiB traversal conditions interference without identifying a cache serving tier.",
                   "L2 is a fixed locality control, not evidence of measured busy pressure.",
                   "Receipt verification is not a rebuild or verification of the worker launch/source snapshot identity.",
                   "Without --source-root, consumer source/derived hashes are retained but not independently reproduced.",
                   "No PMU, IPC, miss occupancy or stall attribution."])
    (root / "summary.json").write_text(json.dumps(out, indent=2) + "\n")
    for name, summary in (("window-summary.csv", window_summary), ("execution-summary.csv", phase_summary)):
        with (root / name).open("w", newline="") as stream:
            if summary:
                writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
                writer.writeheader()
                writer.writerows(summary)
    print(json.dumps(dict(window_cells=len(window_summary), execution_cells=len(phase_summary),
                          policy_cost=out["policy_cost"], qualification=qualification), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--partial", action="store_true")
    parser.add_argument("--qualify", action="store_true", help="require and replay complete native receipts")
    parser.add_argument("--source-root", type=Path, help="captured SixDB source root for consumer hash/derivation verification")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=64)
    args = parser.parse_args()
    if args.repetitions < 1 or args.iterations < 1:
        parser.error("repetitions and iterations must be positive")
    if args.qualify and args.partial:
        parser.error("--qualify requires the complete matrix")
    if args.source_root and not args.qualify:
        parser.error("--source-root requires --qualify")
    try:
        report(args)
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(2, f"Loom locality report failed: {error}\n")


if __name__ == "__main__":
    main()
