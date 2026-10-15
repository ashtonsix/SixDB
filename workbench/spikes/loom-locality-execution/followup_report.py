#!/usr/bin/env python3
"""Qualify alternating locality/unique-line controls without pooling old timings."""
import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import statistics
import subprocess
import sys
import tarfile
import tempfile
from collections import defaultdict
from pathlib import Path, PurePosixPath

import report as initial

SCALES = initial.SCALES
METHODS = initial.METHODS
PHASES = initial.PHASES
EXECUTION_MODES = initial.EXECUTION_MODES
CUTOFFS = initial.CUTOFFS
COST_CASES = initial.COST_CASES
WINDOW_MODES = ("reference", "unique")
VARIANTS = ("full", "budgeted")
require = initial.require
digest = initial.digest
rows = initial.rows
same_inventory = initial.same_inventory
summarize = initial.summarize


def expected_window_cells():
    return {(scale, kind, mode, mixed, concurrency, owner, method)
            for scale, kind, _, mixed, concurrency, owner, method
            in initial.expected_window_cells() for mode in WINDOW_MODES}


def expected_execution_cells():
    return {(scale, kind, variant, mixed, phase, mode, cutoff)
            for scale, kind, mixed, phase, mode, cutoff
            in initial.expected_execution_cells() for variant in VARIANTS}


def targets():
    windows = {f"loom_window_{scale}_{kind}_{mode}"
               for scale in SCALES for kind in range(3) for mode in WINDOW_MODES}
    execution = {f"loom_execution_{scale}_{kind}_{variant}"
                 for scale in SCALES for kind in range(3) for variant in VARIANTS}
    checks = {"loom_contiguous_check", "loom_contiguous_identity_check", "loom_locality_check"}
    return windows, execution, checks


def safe_name(value):
    require(isinstance(value, str) and value, "missing receipt filename")
    path = PurePosixPath(value)
    require(not path.is_absolute() and ".." not in path.parts and path.as_posix() == value,
            f"unsafe receipt filename: {value}")
    return value


def schedule(root, repetitions, hashes):
    path = root / "schedule.json"
    entries = json.loads(path.read_text())
    require(isinstance(entries, list), "schedule must be an array")
    windows, execution, _ = targets()
    expected = {(target, rep) for target in windows | execution for rep in range(repetitions)}
    seen = set()
    position = {}
    for index, item in enumerate(entries):
        require(isinstance(item, dict) and set(item) == {"target", "pass", "csv", "sha256"},
                f"schedule entry fields: {index}")
        target, rep = item["target"], item["pass"]
        require(type(rep) is int, f"noninteger schedule pass: {index}")
        require((target, rep) in expected, f"unexpected schedule target/pass: {target}/{rep}")
        require((target, rep) not in seen, f"duplicate schedule target/pass: {target}/{rep}")
        require(item["csv"] == f"{target}-pass{rep}.csv", f"schedule filename disagrees: {target}/{rep}")
        require(re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) is not None,
                f"invalid schedule hash: {target}/{rep}")
        require(digest(root / item["csv"]) == item["sha256"], f"schedule CSV hash mismatch: {target}/{rep}")
        hashes[item["csv"]] = item["sha256"]
        seen.add((target, rep))
        position[target, rep] = index
    require(seen == expected, f"missing {len(expected - seen)} scheduled target/passes")
    actual_files = {path.name for path in root.glob("loom_*-pass*.csv")}
    require(actual_files == {item["csv"] for item in entries}, "CSV files and schedule differ")
    for scale in SCALES:
        for kind in range(3):
            for rep in range(repetitions):
                window = [f"loom_window_{scale}_{kind}_{mode}" for mode in WINDOW_MODES]
                execution_pair = [f"loom_execution_{scale}_{kind}_{variant}" for variant in VARIANTS]
                if rep % 2:
                    window.reverse()
                    execution_pair.reverse()
                require(position[window[0], rep] < position[window[1], rep],
                        f"window order did not alternate: {scale}/{kind}/{rep}")
                require(position[execution_pair[0], rep] < position[execution_pair[1], rep],
                        f"execution order did not alternate: {scale}/{kind}/{rep}")
                require(position[window[1], rep] < position[execution_pair[0], rep],
                        f"execution preceded window pair: {scale}/{kind}/{rep}")
    hashes[path.name] = digest(path)
    return entries


def inventories(root, entries, hashes):
    out = {}
    per_pass = []
    for item in entries:
        target, rep = item["target"], item["pass"]
        if not target.startswith("loom_window_"):
            continue
        path = root / f"{target}-pass{rep}-inventory.txt"
        require(path.is_file(), f"missing stderr inventory: {path.name}")
        hashes[path.name] = digest(path)
        lines = [line for line in path.read_text().splitlines() if line.startswith("window_inventory,")]
        if not target.endswith("_unique"):
            require(not lines, f"unexpected unique window inventory: {target}")
            continue
        require(len(lines) == 2, f"expected hit/mixed inventories: {target}/{rep}")
        for mixed, line in enumerate(lines):
            match = re.fullmatch(r"window_inventory,line=(64|128),cap=(\d+),largest=(\d+)((?:,\d+:\d+)+)", line)
            require(match is not None, f"malformed inventory: {target}/{rep}/{mixed}")
            geometry, cap, largest = (int(match[i]) for i in (1, 2, 3))
            counts = {}
            for pair in match[4][1:].split(","):
                size, count = map(int, pair.split(":"))
                require(0 <= size <= 1024 and count > 0 and size not in counts,
                        f"invalid inventory size/count: {target}/{rep}/{mixed}")
                counts[size] = count
            require(cap == geometry * 8 and largest == max(counts) and largest > 0,
                    f"inventory geometry/maximum: {target}/{rep}/{mixed}")
            value = dict(mixed=mixed, line_bytes=geometry, cap_bytes=cap, largest_window=largest,
                         counts=counts, cap_lowered=largest > cap,
                         windows_above_cap=sum(count for size, count in counts.items() if size > cap))
            key = (target, mixed)
            if key in out:
                require(value == out[key], f"inventory changed across passes: {target}/{mixed}")
            else:
                out[key] = value
            per_pass.append(dict(target=target, **{"pass": rep}) | value)
    return per_pass


def verify_source(root, source_root, hashes):
    manifest_path, archive_path, job_path = (root / name for name in ("source-manifest.json", "source.tar.gz", "job.json"))
    manifest = json.loads(manifest_path.read_text())
    require(isinstance(manifest, dict) and bool(manifest), "empty source manifest")
    for name, sha in manifest.items():
        safe_name(name)
        require(re.fullmatch(r"[0-9a-f]{64}", sha) is not None, f"invalid source hash: {name}")
    job = json.loads(job_path.read_text())
    identity = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    require(identity == job["source"]["digest"], "launch source digest differs from manifest")
    require(digest(archive_path) == job["source"]["sha256"], "launch source archive hash mismatch")
    with tarfile.open(archive_path, "r:gz") as archive:
        seen = set()
        for member in archive:
            name = safe_name(member.name)
            require(member.isfile() and name in manifest and name not in seen,
                    f"unexpected/duplicate source archive member: {name}")
            stream = archive.extractfile(member)
            require(stream is not None and hashlib.sha256(stream.read()).hexdigest() == manifest[name],
                    f"source archive member hash mismatch: {name}")
            seen.add(name)
        require(seen == set(manifest), "source archive/manifest inventory differs")
    if source_root is not None:
        receipt = source_root.with_name(source_root.name + ".capture.json")
        if receipt.is_file():
            capture = json.loads(receipt.read_text())
            require(capture["source_files_sha256"] == manifest and capture["source_digest"] == identity,
                    "supplied capture differs from launch manifest")
        for name, sha in manifest.items():
            require(digest(source_root / name) == sha, f"supplied source capture changed: {name}")
    for path in (manifest_path, archive_path, job_path):
        hashes[path.name] = digest(path)
    return dict(source_files=len(manifest), launch_source_verified=True, launch_source_digest=identity,
                supplied_capture_verified=source_root is not None)


def verify_compile(root, expected, hashes):
    path = root / "compile.json"
    entries = json.loads(path.read_text())
    require(isinstance(entries, list) and bool(entries), "empty compilation database")
    present = defaultdict(list)
    for item in entries:
        tokens = item.get("arguments") or shlex.split(item["command"])
        for target in expected:
            if any(f"/CMakeFiles/{target}.dir/" in token or token.startswith(f"CMakeFiles/{target}.dir/") for token in tokens):
                present[target].append((item, tokens))
    require(set(present) == expected, f"missing compilation entries: {sorted(expected - set(present))}")
    for target, commands in present.items():
        for item, tokens in commands:
            require("-O3" in tokens and "-std=c++23" in tokens,
                    f"unexpected release/language options: {target}")
            if target.startswith(("loom_window_", "loom_execution_")):
                require("-DNDEBUG" in tokens, f"missing consumer option -DNDEBUG: {target}")
                kind = target.split("_")[3]
                for flag in ("-DHT_STAGED_GRAPH_HINT=1", "-DHT_STAGED_KEY=40", "-DHT_STAGED_WIDTH=8",
                             f"-DHT_STAGED_KIND={kind}", "-fno-vectorize", "-fno-slp-vectorize"):
                    require(flag in tokens, f"missing consumer option {flag}: {target}")
                if target.endswith("_unique"):
                    require("-DLOOM_CONTIGUOUS_IDENTITY=1" in tokens and "-DLOOM_WINDOW_LOCALITY=3" in tokens and
                            Path(item["file"]).name == "consumer-unique.cpp", f"unique consumer compilation: {target}")
                elif target.startswith("loom_window_"):
                    require(Path(item["file"]).name == "staged.cpp", f"reference consumer compilation: {target}")
                else:
                    budgeted = int(target.endswith("_budgeted"))
                    require(f"-DLOOM_BUDGETED_SAMPLE={budgeted}" in tokens and Path(item["file"]).name == "execution.cpp",
                            f"execution variant compilation: {target}")
    hashes[path.name] = digest(path)
    return len(present)


def verify_receipts(root, source_root, hashes):
    windows, execution, checks = targets()
    expected = windows | execution | checks | {"loom_policy_cost"}
    path = root / "binaries.json"
    manifest = json.loads(path.read_text())
    require(set(manifest) == expected, f"binary inventory differs: missing={sorted(expected - set(manifest))}, extra={sorted(set(manifest) - expected)}")
    for target in sorted(expected):
        require(digest(root / target) == manifest[target], f"binary hash mismatch: {target}")
    hashes[path.name] = digest(path)
    check_path = root / "check.txt"
    text = check_path.read_text()
    expected_checks = checks | {target + "_check" for target in windows | execution}
    passed = re.findall(r"Test\s+#\d+:\s+(\S+)\s+\.{2,}\s+Passed\b", text)
    require(len(passed) == len(expected_checks) and set(passed) == expected_checks,
            f"CTest identities differ: expected {len(expected_checks)}, got {len(passed)}")
    require(re.search(rf"100% tests passed, 0 tests failed out of {len(expected_checks)}\b", text) is not None,
            "missing complete successful CTest summary")
    hashes[check_path.name] = digest(check_path)
    here = Path(__file__).resolve().parent
    checker = here / "check_consumer_codegen.py"
    environment = dict(os.environ, PYTHONOPTIMIZE="0")
    for target in sorted(windows):
        assembly, receipt_path = (root / (target + suffix) for suffix in (".asm.gz", "-codegen.json"))
        receipt = json.loads(receipt_path.read_text())
        command = [sys.executable, "-B", str(checker), str(assembly)]
        if int(target.split("_")[3]) < 2:
            command.append("--word-control")
        replay = subprocess.run(command, capture_output=True, text=True, env=environment)
        require(replay.returncode == 0, f"codegen replay failed: {target}: {replay.stderr[-2000:]}")
        require(json.loads(replay.stdout) == receipt, f"codegen receipt differs from retained assembly: {target}")
        for path in (assembly, receipt_path):
            hashes[path.name] = digest(path)
    consumer = {}
    for unique in (False, True):
        name = "consumer-unique.receipt.json" if unique else "consumer.receipt.json"
        receipt_path = root / name
        receipt = json.loads(receipt_path.read_text())
        for field in ("source_sha256", "derived_sha256"):
            require(re.fullmatch(r"[0-9a-f]{64}", receipt.get(field, "")) is not None, f"invalid {name} {field}")
        if source_root is not None:
            source = source_root / "workbench/spikes/hashtable-design/staged.cpp"
            require(digest(source) == receipt["source_sha256"], f"consumer source hash differs: {name}")
            with tempfile.TemporaryDirectory(prefix="loom-followup-consumer-") as temporary:
                derived = Path(temporary) / "consumer.cpp"
                command = [sys.executable, "-B", str(here / "prepare_consumer.py"), str(source), str(derived)]
                if unique:
                    command.append("--unique")
                replay = subprocess.run(command, capture_output=True, text=True, env=environment)
                require(replay.returncode == 0, f"controlled consumer derivation failed: {name}: {replay.stderr[-2000:]}")
                require(digest(derived) == receipt["derived_sha256"], f"consumer derivation does not reproduce: {name}")
        consumer[name] = {key: receipt[key] for key in ("source_sha256", "derived_sha256")}
        hashes[receipt_path.name] = digest(receipt_path)
    require(len({v["source_sha256"] for v in consumer.values()}) == 1, "consumer source differs between controls")
    source = verify_source(root, source_root, hashes)
    compile_targets = verify_compile(root, expected, hashes)
    hardware = json.loads((root / "hardware.json").read_text())
    require(hardware["line_bytes"] in (64, 128), "unsupported retained hardware line geometry")
    for path in (root / "compiler.txt", root / "hardware.json", root / "host.json", root / "worker-result.json"):
        hashes[path.name] = digest(path)
    result = json.loads((root / "worker-result.json").read_text())
    require(result["state"] == "complete" and result["script_returncode"] == 0, "worker did not complete successfully")
    return dict(checks=len(expected_checks), binary_hashes=len(expected), codegen_replays=len(windows),
                compile_targets=compile_targets, consumer_source_verified=source_root is not None,
                consumer_derived_verified=source_root is not None, consumers=consumer,
                hardware_line_bytes=hardware["line_bytes"], **source,
                tools={path.name: digest(path) for path in
                       (Path(__file__), here / "report.py", here / "prepare_consumer.py", checker)})


def paired_windows(groups):
    result = []
    for key, reference in sorted(groups.items()):
        if key[2] != "reference":
            continue
        counterpart = key[:2] + ("unique",) + key[3:]
        candidate = {int(row["rep"]): row for row in groups[counterpart]}
        ratios = []
        differences = []
        for row in sorted(reference, key=lambda row: int(row["rep"])):
            other = candidate[int(row["rep"])]
            before, after = float(row["ns_per_request"]), float(other["ns_per_request"])
            ratios.append(after / before)
            differences.append(after - before)
        result.append(dict(zip(("scale", "kind", "mixed", "concurrency", "owner", "method"), key[:2] + key[3:])) |
                      dict(unique_over_reference_median=statistics.median(ratios),
                           unique_over_reference_min=min(ratios), unique_over_reference_max=max(ratios),
                           unique_minus_reference_median=statistics.median(differences),
                           pass_ratios=ratios, pass_differences=differences))
    return result


def report(args):
    root = args.root
    windows, phase, cost = (defaultdict(list) for _ in range(3))
    inventory, hashes = {}, {}
    entries = schedule(root, args.repetitions, hashes)
    window_inventory = inventories(root, entries, hashes)
    for item in entries:
        path, rep = root / item["csv"], item["pass"]
        for original in rows(path):
            require(int(original["rep"]) == 0, f"raw repetition must be zero in single-pass process: {path.name}")
            row = dict(original, rep=str(rep))
            mixed = int(row["mixed"])
            match = re.fullmatch(r"loom_window_(hot|large)_([0-2])_(reference|unique)", item["target"])
            if match:
                scale, kind, mode = match.groups()
                kind, concurrency = int(kind), int(row["concurrency"])
                require(int(row["kind"]) == kind and mixed in (0, 1), f"window kind/mixed mismatch: {path.name}")
                require(int(row["K"]) == 40 and int(row["W"]) == (0 if kind == 0 else 8), f"window key/value shape: {path.name}")
                geometry = 9 if scale == "hot" and kind == 0 else 10 if scale == "hot" else 20 if kind == 0 else 19
                require(int(row["A"]) == geometry and int(row["B"]) == geometry - 4 and
                        int(row["unit"]) == (64 if kind == 0 else 128 if kind == 1 else 192), f"window geometry: {path.name}")
                keys = initial.keys_for(scale, kind)
                graph = keys * (mixed + 1)
                requests = graph * max(1, (262144 + graph - 1) // graph)
                require(int(row["keys"]) == keys and int(row["requests"]) == requests and int(row["graph_hints"]) == 1,
                        f"window request graph differs: {path.name}")
                require(int(row["ring_visits"]) >= 0 and bool(int(row["ring_visits"])) == (row["method"] in ("ring", "ring_hints")),
                        f"window Ring visit inventory: {path.name}")
                key = (scale, kind, mode, mixed, concurrency, row["owner"], row["method"])
                windows[key].append(row)
                same_inventory(inventory, ("window", scale, kind, mixed, concurrency, row["owner"], row["method"]), row,
                               ("requested", "metadata", "jobs_bytes", "graph_bytes", "ring_bytes", "ring_visits", "graph_hints", "requests"))
            else:
                match = re.fullmatch(r"loom_execution_(hot|large)_([0-2])_(full|budgeted)", item["target"])
                require(match is not None, f"unexpected target CSV: {path.name}")
                scale, kind, variant = match.groups()
                kind, mode = int(kind), row["mode"]
                budgeted, probe = int(variant == "budgeted"), 32 if variant == "budgeted" else 256
                require(int(row["budgeted"]) == budgeted and int(row["probe_requests"]) == probe,
                        f"execution sampler variant differs: {path.name}")
                for field, expected in (("probe_heads", 4 if budgeted else 32),
                                        ("probe_groups", 8 if budgeted else 1),
                                        ("sample_cadence_batches", 32), ("early_reprobe", budgeted),
                                        ("sampler_bytes", 24 if budgeted else 0)):
                    require(int(row[field]) == expected, f"execution sampler metadata {field}: {path.name}")
                require(int(row["kind"]) == kind and mixed in (0, 1) and int(row["keys"]) == initial.keys_for(scale, kind),
                        f"execution owner shape: {path.name}")
                direct, ring = int(row["direct_batches"]), int(row["ring_batches"])
                sample_requests, sample_ns = int(row["sample_requests"]), int(row["sample_ns"])
                require(direct >= 0 and ring >= 0 and direct + ring == args.iterations and
                        int(row["requests"]) == args.iterations * 256, f"execution useful work: {path.name}")
                require(0 <= sample_requests <= args.iterations * probe and sample_requests % probe == 0 and
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
                key = (scale, kind, variant, mixed, row["phase"], mode, int(row["cutoff"]))
                phase[key].append(row)
                same_inventory(inventory, ("execution", scale, kind, mixed, row["phase"]), row,
                               ("requests", "checksum", "record_bytes", "graph_bytes", "interference_bytes"))
                same_inventory(inventory, ("trace", scale, kind, variant, mixed), row, ("trace_bytes",))
    cost_path = root / "policy-cost.csv"
    for row in rows(cost_path):
        case = row["case"]
        require(case in COST_CASES, f"unexpected policy-cost case: {case}")
        require(int(row["operations"]) == 1000000 and int(row["record_bytes"]) == 56 and
                int(row["checksum"]) == COST_CASES[case] * 1000000, f"policy-cost useful outcome: {case}")
        cost[case].append(row)
    hashes[cost_path.name] = digest(cost_path)
    for label, actual, expected in (("window", windows, expected_window_cells()),
                                    ("execution", phase, expected_execution_cells()),
                                    ("policy-cost", cost, set(COST_CASES))):
        initial.exact_cells(actual, expected, label, False)
        for key, group in actual.items():
            initial.repetitions(group, args.repetitions, f"{label} {key}", False)
    qualification = dict(complete_matrix=True, repetitions=args.repetitions, scheduled_processes=len(entries),
                         execution_iterations=args.iterations, receipts_verified=False,
                         window_cells=len(windows), execution_cells=len(phase))
    if args.qualify:
        qualification |= verify_receipts(root, args.source_root, hashes)
        qualification["receipts_verified"] = True
        require(all(item["line_bytes"] == qualification["hardware_line_bytes"] for item in window_inventory),
                "window inventory/hardware line geometry differs")
    window_summary = [dict(zip(("scale", "kind", "mode", "mixed", "concurrency", "owner", "method"), key)) | summarize(group)
                      for key, group in sorted(windows.items())]
    phase_summary = [dict(zip(("scale", "kind", "variant", "mixed", "phase", "mode", "cutoff"), key)) | summarize(group) |
                     {field: [int(row[field]) for row in sorted(group, key=lambda row: int(row["rep"]))]
                      for field in ("sample_requests", "sample_ns", "direct_batches", "ring_batches", "decision_calls", "probe_requests", "trace_bytes", "sampler_bytes")}
                     for key, group in sorted(phase.items())]
    paired = paired_windows(windows)
    out = dict(window=window_summary, execution=phase_summary, paired_window=paired, window_inventory=window_inventory,
               policy_cost={key: summarize(group) for key, group in cost.items()}, input_hashes=hashes, qualification=qualification,
               limits=[
                   "Window controls use complete result-dependent graphs; execution controls use charged complete-query prefixes.",
                   "Each pass runs a separate process. Alternating mode order and pass pairing reduce order bias but do not prove identical operating conditions.",
                   "Window ratios pair pass IDs; execution summaries retain individual variant variation without pooling previous campaigns.",
                   "Unique procedures cap coverage at eight native lines; actual window-size inventory identifies any larger windows.",
                   "The largest unique class may issue a duplicate tail hint when an eight-line prefix intersects only eight physical lines.",
                   "Window storage equality covers original owner/job inventory; unique adds a 1025-entry prepared shape table and untimed inventory storage.",
                   "Sampling is extra work and does not advance useful requests. Full probes complete 256 serial queries; budgeted probes complete 32.",
                   "Policy predicts cheap serial cost, never cache residency; small probes may misrepresent heterogeneity.",
                   "Generation/domain/expiry phases are fixture identity controls; no real migration.",
                   "The 128 MiB traversal conditions interference without identifying a cache serving tier.",
                   "Receipt/source/compile verification is not a rebuild or independent hardware performance measurement.",
                   "Without --source-root, consumer derivation hashes are retained but not independently reproduced.",
                   "No PMU, IPC, miss occupancy or stall attribution."])
    (root / "summary.json").write_text(json.dumps(out, indent=2) + "\n")
    for name, summary in (("window-summary.csv", window_summary), ("execution-summary.csv", phase_summary),
                          ("paired-window-summary.csv", paired)):
        with (root / name).open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
    print(json.dumps(dict(qualification=qualification, policy_cost=out["policy_cost"],
                          window_inventory=[{key: value for key, value in item.items() if key != "counts"}
                                            for item in window_inventory]), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--qualify", action="store_true", help="replay complete native binary/source/check/codegen receipts")
    parser.add_argument("--source-root", type=Path, help="immutable captured source for consumer and launch verification")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=64)
    args = parser.parse_args()
    if args.repetitions < 1 or args.iterations < 1:
        parser.error("repetitions and iterations must be positive")
    if args.source_root and not args.qualify:
        parser.error("--source-root requires --qualify")
    try:
        report(args)
    except (ValueError, KeyError, OSError, TypeError, tarfile.TarError) as error:
        parser.exit(2, f"Loom locality followup report failed: {error}\n")


if __name__ == "__main__":
    main()
