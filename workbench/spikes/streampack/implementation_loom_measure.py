#!/usr/bin/env python3
"""Retain sequential pinned StreamPack Loom CLI samples; never compile.

Example pilot:
  python3 implementation_loom_measure.py --binary /absolute/implementation_loom_check \
    --output /absolute/new-output --cpu 0 --tiers hot --families width --routes manual ring --widths 8
Default selection is 64 processes: two families, two populations, 16 arms each.
Use --cases JSON for an explicit ordered case list, or --summarize OUTPUT to
reparse retained logs without running the executable.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
TIERS = {"hot": (4, 8192), "large": (16384, 262144)}
COUNTERS = ("accepted", "refused", "completed", "promoted", "visits", "quanta", "refill_boundaries", "consumed")
LIMITATIONS = [
    "Local Linux VM screening; reported cache geometry does not establish actual cache residence or DRAM behavior.",
    "Payload capacity and distinct demanded first8/tail4 cache lines are separate quantities; line coverage is logical, not measured misses.",
    "Each executable process asserts every Outcome equals its independent application oracle and source byte checksum is unchanged; no serialized digest equality is inferred.",
    "No PMU, IPC, stall or native fleet-worker measurements; no HardwareService or background calibration is attached.",
    "Three contiguous samples per process screen this caller; sequential arm order can expose time drift and does not establish a universal scheduling policy.",
    "Samples include admission/refusal/refill/delivery/drain; printed setup/close and whole-process resource measurements are separate.",
    "CLI frames_bytes combines Frame and Outcome storage; outcome_bytes is an overlapping breakdown, not additional inventory. Pure Frame bytes subtract that breakdown before normalization.",
    "Continuation tuple bytes require the caller codegen receipt and are not inferred from scheduler capacity.",
]


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def text(path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def context(cpu):
    root = Path(f"/sys/devices/system/cpu/cpu{cpu}")
    caches = []
    for entry in sorted((root / "cache").glob("index*")):
        caches.append({name: text(entry / name) for name in
                       ("level", "type", "size", "coherency_line_size", "shared_cpu_list")})
    info = text(Path("/proc/cpuinfo")) or ""
    selected = []
    for line in info.splitlines():
        if re.match(r"^(model name|vendor_id|CPU implementer|CPU architecture|CPU part|CPU revision|Hardware)\s*:", line):
            if line not in selected:
                selected.append(line)
    return {"host": platform.node(), "platform": platform.platform(), "machine": platform.machine(),
            "cpu": cpu, "allowed_affinity": sorted(os.sched_getaffinity(0)),
            "reported_caches": caches, "cpu_descriptions": selected,
            "governor": text(root / "cpufreq/scaling_governor"),
            "kernel": text(Path("/proc/version")),
            "dmi_product": text(Path("/sys/class/dmi/id/product_name")),
            "hypervisor_flag_reported": " hypervisor " in f" {info.replace(chr(10), ' ')} "}


def cases(args):
    if args.cases:
        raw = json.loads(args.cases.read_text())
        result = raw if isinstance(raw, list) else raw["cases"]
    else:
        result = []
        for tier in args.tiers:
            blocks, requests = TIERS[tier]
            for family in args.families:
                for placement in args.placements:
                    for route in args.routes:
                        widths = [8] if route in ("direct", "point-chain") else args.widths
                        for width in widths:
                            for hints in args.hints:
                                name = f"{tier}-{family}-{placement}-{route}-c{width}-h{hints}"
                                result.append(dict(name=name, tier=tier, family=family, placement=placement,
                                                   blocks=blocks, requests=requests, width=width,
                                                   reps=args.reps, route=route, hints=hints))
    if args.reverse:
        result.reverse()
    names = set()
    for case in result:
        if not re.fullmatch(r"[A-Za-z0-9_-]+", case["name"]) or case["name"] in names:
            raise ValueError("cases need unique simple names")
        names.add(case["name"])
        if case["family"] not in ("direct", "width") or case["placement"] not in ("grouped", "separated"):
            raise ValueError(f"{case['name']}: invalid family/placement")
        if case["route"] not in ("direct", "point-chain", "manual", "ring"):
            raise ValueError(f"{case['name']}: invalid route")
        if any(type(case[key]) is not int for key in ("blocks", "requests", "width", "reps", "hints")):
            raise ValueError(f"{case['name']}: integer dimensions required")
        if not (1 <= case["blocks"] <= 65536 and 1 <= case["requests"] <= 1048576
                and case["width"] in (1, 8, 32) and 3 <= case["reps"] <= 100 and case["hints"] in (0, 1)):
            raise ValueError(f"{case['name']}: dimensions outside bounded CLI")
    if not result:
        raise ValueError("select at least one case")
    return result


def fields(line):
    result = {}
    for key, value in re.findall(r"(\w+)=([^\s;]+)", line):
        try:
            result[key] = int(value)
        except ValueError:
            try:
                result[key] = float(value)
            except ValueError:
                result[key] = value
    return result


def parse(path, case):
    data = {"samples": []}
    for line in path.read_text().splitlines():
        if line.startswith("sample="):
            data["samples"].append(fields(line))
        for prefix, key in (("family=", "configuration"), ("setup ", "setup"),
                            ("footprint ", "footprint"), ("demand_coverage ", "coverage")):
            if line.startswith(prefix):
                if key in data:
                    raise ValueError(f"{case['name']}: duplicate {key} output")
                data[key] = fields(line)
    cfg = data["configuration"]
    for key in ("family", "placement", "blocks", "requests", "width", "route", "hints"):
        if cfg[key] != case[key]:
            raise ValueError(f"{case['name']}: CLI output configuration differs at {key}")
    if cfg["assigned"] != 256 or cfg["overflow"] != 4 or cfg["quantum"] != 128:
        raise ValueError(f"{case['name']}: unexpected consumer setup")
    setup, coverage = data["setup"], data["coverage"]
    if setup["row_oracle_released"] != 1 or coverage["scratch_released"] != 1:
        raise ValueError(f"{case['name']}: oracle/coverage scratch retained during timing")
    if (coverage["distinct_sources"] > case["blocks"] or
            coverage["distinct_source_ranks"] > case["blocks"] * cfg["assigned"] or
            coverage["consumed"] < coverage["distinct_source_ranks"] or
            coverage["payload_lines"] > coverage["distinct_source_ranks"] * 4 or
            coverage["payload_line_bytes"] != coverage["payload_lines"] * coverage["line_bytes"]):
        raise ValueError(f"{case['name']}: inconsistent logical coverage")
    samples = data["samples"]
    if len(samples) != case["reps"] or [row["sample"] for row in samples] != list(range(case["reps"])):
        raise ValueError(f"{case['name']}: missing/reordered raw samples")
    for row in samples:
        if row["completed"] != case["requests"] or row["consumed"] != coverage["consumed"]:
            raise ValueError(f"{case['name']}: work counts differ from independent trace")
        if any(type(row[key]) is not int or row[key] < 0 for key in COUNTERS + ("elapsed_ns",)):
            raise ValueError(f"{case['name']}: invalid timing/counter")
        for label, count in (("ns_per_request", row["completed"]), ("ns_per_consumed", row["consumed"])):
            expected = row["elapsed_ns"] / count if count else 0.0
            if not math.isfinite(row[label]) or abs(row[label] - expected) > 0.000001:
                raise ValueError(f"{case['name']}: normalized timing does not match elapsed/count")
        if any(row[key] != samples[0][key] for key in COUNTERS):
            raise ValueError(f"{case['name']}: logical work changed between repetitions")
        if case["route"] in ("manual", "ring"):
            nonzero = case["requests"] - (case["requests"] + 10) // 11
            if row["visits"] != 2 * row["consumed"] or row["accepted"] != nonzero:
                raise ValueError(f"{case['name']}: native stage/admission count changed")
    data["case"] = case
    data["median_ns_per_request"] = statistics.median(row["elapsed_ns"] / row["completed"] for row in samples)
    data["median_ns_per_consumed"] = (statistics.median(row["elapsed_ns"] / row["consumed"] for row in samples)
                                       if samples[0]["consumed"] else None)
    if setup["frames_bytes"] < setup["outcome_bytes"]:
        raise ValueError(f"{case['name']}: Frame/Outcome inventory breakdown exceeds total")
    data["frame_bytes_per_request"] = (setup["frames_bytes"] - setup["outcome_bytes"]) / case["requests"]
    data["frame_outcome_bytes_per_request"] = setup["frames_bytes"] / case["requests"]
    return data


def group_key(case):
    return (case.get("tier", "custom"), case["family"], case["placement"], case["blocks"], case["requests"])


def summarize(output):
    receipt = json.loads((output / "measure.json").read_text())
    rows = []
    for trial in receipt["trials"]:
        if trial.get("returncode") != 0:
            continue
        log = output / trial["log"]
        if digest(log) != trial["log_sha256"]:
            raise ValueError(f"retained output hash changed: {log}")
        rows.append(parse(log, trial["case"]))
    groups = {}
    for row in rows:
        groups.setdefault(group_key(row["case"]), []).append(row)
    comparisons = []
    for key, group in groups.items():
        first = group[0]
        coverage_keys = ("consumed", "distinct_sources", "distinct_source_ranks", "payload_lines", "payload_line_bytes", "line_bytes", "page_bytes")
        for row in group:
            if any(row["coverage"][name] != first["coverage"][name] for name in coverage_keys):
                raise ValueError(f"{key}: matched arms have different independent demand coverage")
            if row["samples"][0]["completed"] != first["samples"][0]["completed"]:
                raise ValueError(f"{key}: matched arms have different request work")
        by_arm = {(row["case"]["route"], row["case"]["width"], row["case"]["hints"]): row for row in group}
        pairs = []
        for (route, width, hints), ring in by_arm.items():
            manual = by_arm.get(("manual", width, hints))
            if route != "ring" or manual is None:
                continue
            if any(ring["samples"][0][name] != manual["samples"][0][name] for name in COUNTERS):
                raise ValueError(f"{key}: manual/Ring scheduling differs for C{width}/h{hints}")
            pairs.append({"width": width, "hints": hints, "manual": manual["case"]["name"],
                          "ring": ring["case"]["name"],
                          "manual_over_ring": ratio(manual, ring)})
        baseline = by_arm.get(("direct", 8, 0))
        controls = [row for row in group if row["case"]["route"] != "ring"]
        rings = [row for row in group if row["case"]["route"] == "ring"]
        positive = lambda row: row["median_ns_per_consumed"] if row["median_ns_per_consumed"] is not None else math.inf
        best_control = min(controls, key=positive) if controls else None
        best_ring = min(rings, key=positive) if rings else None
        comparisons.append({"group": list(key), "matched_payload_coverage": first["coverage"],
                            "direct_no_hint": baseline["case"]["name"] if baseline else None,
                            "best_control": best_control["case"]["name"] if best_control else None,
                            "best_ring": best_ring["case"]["name"] if best_ring else None,
                            "best_control_over_best_ring": ratio(best_control, best_ring),
                            "manual_ring_pairs": pairs})
        for row in group:
            row["direct_no_hint_over_arm"] = ratio(baseline, row)
    runner_hashes = {"execution_runner_sha256": receipt["runner_sha256"],
                     "analysis_runner_sha256": digest(Path(__file__))}
    summary = {"format": 1, "status": receipt["status"], "binary_sha256": receipt["binary_sha256"],
               **runner_hashes, "limitations": LIMITATIONS, "cases": rows, "comparisons": comparisons}
    save(output / "summary.json", summary)
    compact = {"format": 1, "status": receipt["status"], **runner_hashes, "limitations": LIMITATIONS,
               "comparisons": comparisons, "medians": [
                   {"case": row["case"]["name"], "ns_per_consumed": row["median_ns_per_consumed"],
                    "ns_per_request": row["median_ns_per_request"],
                    "direct_no_hint_over_arm": row["direct_no_hint_over_arm"],
                    "frame_bytes_per_request": row["frame_bytes_per_request"],
                    "frame_outcome_bytes_per_request": row["frame_outcome_bytes_per_request"]} for row in rows]}
    save(output / "comparison.json", compact)
    return len(rows)


def ratio(a, b):
    if a is None or b is None or not a["median_ns_per_consumed"] or not b["median_ns_per_consumed"]:
        return None
    return a["median_ns_per_consumed"] / b["median_ns_per_consumed"]


def run(args):
    if not hasattr(os, "sched_getaffinity") or args.cpu not in os.sched_getaffinity(0):
        raise ValueError("run on Linux and choose an allowed CPU")
    selected = cases(args)
    binary = args.binary.expanduser().resolve(strict=True)
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    retained = output / "implementation_loom_check"
    shutil.copy2(binary, retained)
    receipt = {"format": 1, "status": "planned" if args.plan_only else "running",
               "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
               "original_binary": str(binary), "binary_sha256": digest(retained),
               "runner_sha256": digest(Path(__file__)), "context": context(args.cpu),
               "cases": selected, "limitations": LIMITATIONS, "trials": []}
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    if args.source_receipt:
        shutil.copy2(args.source_receipt, output / "source-receipt")
        receipt["source_receipt"] = {"path": str(args.source_receipt.resolve()),
                                     "sha256": digest(output / "source-receipt"),
                                     "qualification": "caller-supplied build receipt; runner does not compile or establish its linkage"}
    save(output / "measure.json", receipt)
    save(output / "cases.json", {"cases": selected})
    if args.plan_only:
        print(f"Plan only: {len(selected)} cases in {output}", flush=True)
        return
    try:
        for index, case in enumerate(selected, 1):
            name = f"{index:02d}-{case['name']}"
            log = output / (name + ".txt")
            resources = output / (name + ".resources.json")
            command = ["taskset", "-c", str(args.cpu), "/usr/bin/time", "-q", "-o", str(resources),
                       "-f", '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}',
                       str(retained), "bench", case["family"], str(case["blocks"]), str(case["requests"]),
                       str(case["width"]), str(case["reps"]), case["route"], str(case["hints"]), case["placement"]]
            trial = {"case": case, "argv": command, "cwd": str(ROOT), "log": log.name}
            receipt["trials"].append(trial)
            save(output / "measure.json", receipt)
            print(f"[{index}/{len(selected)}] {case['name']}", flush=True)
            started = time.monotonic()
            with log.open("w") as stream:
                completed = subprocess.run(command, cwd=ROOT, env=os.environ | {"SIXDB_CPU": str(args.cpu)},
                                           stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
            trial.update(returncode=completed.returncode, process_seconds=time.monotonic() - started,
                         log_sha256=digest(log))
            if resources.exists():
                trial["resources"] = json.loads(resources.read_text())
            if completed.returncode:
                raise RuntimeError(f"{case['name']} exited {completed.returncode}; see {log}")
            parse(log, case)
            save(output / "measure.json", receipt)
        receipt["status"] = "complete"
    except Exception as error:
        receipt.update(status="failed", error=str(error))
        raise
    finally:
        save(output / "measure.json", receipt)
    count = summarize(output)
    print(f"Retained {count} cases: {output / 'comparison.json'}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--cases", type=Path, help="ordered list or object with cases; complete CLI dimensions per row")
    parser.add_argument("--tiers", choices=TIERS, nargs="+", default=list(TIERS))
    parser.add_argument("--families", choices=("direct", "width"), nargs="+", default=["direct", "width"])
    parser.add_argument("--placements", choices=("grouped", "separated"), nargs="+", default=["grouped"])
    parser.add_argument("--routes", choices=("direct", "point-chain", "manual", "ring"), nargs="+",
                        default=["direct", "point-chain", "manual", "ring"])
    parser.add_argument("--widths", choices=(1, 8, 32), type=int, nargs="+", default=[1, 8, 32])
    parser.add_argument("--hints", choices=(0, 1), type=int, nargs="+", default=[0, 1])
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=120, help="whole-process limit per selected case")
    parser.add_argument("--reverse", action="store_true", help="reverse selected process order")
    parser.add_argument("--plan-only", action="store_true", help="retain plan/binary/context without running cases")
    parser.add_argument("--source-receipt", type=Path)
    parser.add_argument("--summarize", type=Path, help="reparse retained outputs; no executable run")
    args = parser.parse_args()
    if args.summarize:
        print(f"Summarized {summarize(args.summarize.resolve())} retained cases")
    else:
        if args.binary is None or args.output is None or args.cpu is None:
            parser.error("running/planning requires --binary, --output and --cpu")
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            parser.error("timeout must be positive and finite")
        run(args)


if __name__ == "__main__":
    main()
