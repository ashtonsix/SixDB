#!/usr/bin/env python3
"""Select current-source TLC receipts, expose missing cases, optionally bundle evidence."""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

import check
from catalog import read_catalog

SPEC = Path(__file__).resolve().parent
ROOT = SPEC.parents[1]
SEARCH = [ROOT / "build" / name for name in
          ("orbital-formal", "orbital-spec", "orbital-spec-restart", "orbital-tla", "orbital-assistant")]
ACCEPTED = {"complete", "expected_violation", "witnessed"}
CONTRADICTIONS = {"unexpected_violation", "missing_expected_violation"}
PARSED = re.compile(r"^Parsing file (.+?\.tla)(?:\s|$)")


def receipts(root: Path):
    # TLC scratch can contain millions of files; receipts live beside it.
    for directory, children, files in os.walk(root):
        children[:] = [name for name in children if name not in
                       {"metadir", "states", "lineage", "sources", "suite-source", "catalog-inputs", "__pycache__"}]
        for name in ("result.json", "resume-result.json"):
            if name in files:
                yield Path(directory) / name


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n")


def expected_temporal_violation(parsed: check.Output, expected: str) -> bool:
    violations = [m for m in parsed.messages if m["code"] == 2116]
    # The caller also verifies that this is the sole configured property. Older
    # TLC diagnostics omit its name; a different explicit name is contradictory.
    return len(violations) == 1 and violations[0]["text"] in {
        f"Temporal property {expected.split(':', 1)[1]} was violated.",
        "Temporal properties were violated.",
    }


def counterexample_oom(parsed: check.Output, expected: str, data: dict) -> bool:
    if (data.get("status") != "unexpected_violation" or data.get("returncode") != 1
            or not expected_temporal_violation(parsed, expected)):
        return False
    errors = [m for m in parsed.messages if m["level"] in {1, 2}]
    codes = [m["code"] for m in errors]
    if (not codes or codes[-1] != 1003 or codes.count(1003) != 1
            or codes.count(2116) != 1 or 2264 not in codes
            or not set(codes) <= {2116, 2120, 2121, 2264, 1003}
            or {2107, 2110, 2146, 2114}.intersection(parsed.ids)):
        return False
    return (codes.index(2116) < codes.index(2264) < codes.index(1003)
            and errors[-1]["text"].startswith("Java ran out of memory during liveness checking."))


def inspect_receipt(path: Path, case: dict, current: dict[str, str], pin: str) -> dict:
    """Recheck captured bytes and raw diagnostics, rather than trusting a status label."""
    receipt_bytes = path.read_bytes()
    data = json.loads(receipt_bytes)
    expected = case.get("expect")
    witness = case.get("witness")
    purpose = "reachability" if witness else "negative_control" if expected else "check"
    identity = {"module": case["module"] + ".tla", "configuration": case["config"],
                "expectation": expected or "complete", "witness": witness, "purpose": purpose,
                "workers": case.get("workers", 1)}
    if any(data.get(key) != value for key, value in identity.items()):
        raise ValueError("case identity/expectation differs")
    recovery = None
    if path.name == "resume-result.json":
        import recover
        recovery = recover.validate_receipt(path, data, case, current, pin)
        runner_file, runner_hash = "recover.py", data["producer_sha256"]
    else:
        if (data.get("runner_sha256") != current["check.py"] or
                sha((path.parent / "check.py").read_bytes()) != current["check.py"]):
            raise ValueError("runner differs")
        runner_file, runner_hash = "check.py", current["check.py"]
    tool_bytes = (path.parent / "tool-source.json").read_bytes()
    if (data.get("jar", {}).get("sha256") != pin or
            json.loads(tool_bytes).get("sha256") != pin or
            data["jar"].get("source_metadata_sha256") != sha(tool_bytes)):
        raise ValueError("pinned tool differs")
    source = Path(data["cwd"])
    dependencies = {case["config"]}
    def require_current(name: str) -> None:
        if not current.get(name) or data.get("source_sha256", {}).get(name) != current[name]:
            raise ValueError("current dependency differs: " + name)
    require_current(case["config"])
    require_current(case["module"] + ".tla")
    parsed = check.Output()
    parsed_names, semantic_names = set(), set()
    log_hash = hashlib.sha256()
    with (path.parent / "tlc.log").open("rb") as log:
        for line in log:
            log_hash.update(line)
            text = line.decode("utf-8", errors="replace")
            parsed.feed(text, 0)
            semantic = re.match(r"^Semantic processing of module ([A-Za-z_][A-Za-z_0-9]*)", text)
            if semantic:
                semantic_names.add(semantic[1])
            match = PARSED.match(text)
            if not match:
                continue
            imported = Path(match[1])
            parsed_names.add(imported.stem)
            if imported.is_relative_to(source):
                name = imported.relative_to(source).as_posix()
                require_current(name)
                dependencies.add(name)
            elif "(jar:file:" in text and "!/tla2sany/StandardModules/" in text:
                if imported.name in current:
                    raise ValueError("a current local module shadows a formerly standard import")
            else:
                raise ValueError("parsed dependency is outside captured sources and the pinned jar")
    parsed.feed("\n", 0)
    if not semantic_names or parsed_names != semantic_names:
        raise ValueError("parsed and semantic module lists disagree")
    if case["module"] + ".tla" not in dependencies:
        raise ValueError("main module absent from parsed inputs")
    hashes = {}
    for name in sorted(dependencies):
        recorded = data.get("source_sha256", {}).get(name)
        if not recorded or current.get(name) != recorded:
            raise ValueError("current dependency differs: " + name)
        if sha((path.parent / "sources" / name).read_bytes()) != recorded:
            raise ValueError("captured dependency corrupt: " + name)
        hashes[name] = recorded
    if expected and expected.startswith("temporal:"):
        config = (path.parent / "sources" / case["config"]).read_text()
        if check.temporal_properties(config) != [expected.split(":", 1)[1]]:
            raise ValueError("temporal expectation is not the sole configured property")
    status = data.get("status", "running")
    if status in {"running", "incomplete_timeout", "incomplete_interrupted"}:
        # A stopped prefix cannot become completed evidence by reparsing its log.
        verified = status
    else:
        verified, _ = check.classify(parsed, data.get("returncode"), None, expected, witness)
        if status != verified or data.get("states") != parsed.stats:
            raise ValueError("raw diagnostic disagrees with receipt")
    result = {"name": case["name"], "result": str(path.resolve()), "status": verified,
            "purpose": purpose, "expectation": expected or "complete", "witness": witness,
            "started_utc": data.get("started_utc", ""), "states": parsed.stats,
            "returncode": data.get("returncode"),
            "elapsed_seconds": data.get("elapsed_seconds"), "depth": data.get("depth"),
            "peak_rss_bytes": data.get("peak_rss_bytes"), "workers": data.get("workers"),
            "heap": data.get("heap"), "java": data.get("java"),
            "dependency_sha256": hashes, "runner_sha256": runner_hash,
            "runner_file": runner_file, "receipt_name": path.name,
            "jar_sha256": pin, "tool_source_sha256": sha(tool_bytes),
            "receipt_sha256": sha(receipt_bytes),
            "log_sha256": log_hash.hexdigest()}
    if recovery:
        result["recovery_lineage"] = recovery
        result["recovered_states"] = (data.get("recovery") or {}).get("states")
        result["classifier_sha256"] = current["check.py"]
        paths = [p for p in path.parent.iterdir() if p.name in recover.BUNDLE_FILES and p.is_file()]
        paths.extend(p for p in (path.parent / "lineage").rglob("*") if p.is_file())
        result["recovery_inputs_sha256"] = {p.relative_to(path.parent).as_posix(): sha(p.read_bytes()) for p in paths}
    if expected and expected.startswith("temporal:") and 2116 in parsed.ids:
        result["temporal_property_matches"] = expected_temporal_violation(parsed, expected)
        if counterexample_oom(parsed, expected, data):
            result["diagnostic_failure"] = {"kind": "temporal_counterexample_oom",
                                            "messages": parsed.messages}
    return result


def collect(cases: list[dict], searches: list[Path], spec: Path = SPEC) -> dict:
    current = {p.relative_to(spec).as_posix(): sha(p.read_bytes())
               for p in spec.rglob("*") if p.is_file() and p.suffix in {".tla", ".cfg"}}
    current["check.py"] = sha((spec / "check.py").read_bytes())
    pin = check.JAR_SHA256
    by_identity = {(c["module"] + ".tla", c["config"], c.get("expect", "complete"),
                    c.get("witness"), c.get("workers", 1)): c for c in cases}
    if len(by_identity) != len(cases):
        raise ValueError("Two catalog entries select the same receipt identity")
    candidates = {c["name"]: [] for c in cases}
    rejected = Counter()
    seen = set()
    for search in searches:
        for path in receipts(search):
            if path.resolve() in seen:
                continue
            seen.add(path.resolve())
            try:
                data = json.loads(path.read_bytes())
                if not isinstance(data, dict):
                    continue
                identity = (data.get("module"), data.get("configuration"),
                            data.get("expectation"), data.get("witness"), data.get("workers"))
                case = by_identity.get(identity)
                if case:
                    candidates[case["name"]].append(inspect_receipt(path, case, current, pin))
            except (ValueError, OSError, KeyError, TypeError) as error:
                rejected[str(error)] += 1
    rows = []
    for case in cases:
        runs = sorted(candidates[case["name"]], key=lambda r: (r["started_utc"], r["result"]), reverse=True)
        selected = next((r for r in runs if r["status"] in ACCEPTED
                         and r.get("temporal_property_matches") is not False), None)
        conflicts, reconciled = [], []
        for run in runs:
            if run["status"] not in CONTRADICTIONS and run.get("temporal_property_matches") is not False:
                continue
            if (run.get("diagnostic_failure") and selected
                    and selected["status"] in {"expected_violation", "witnessed"}
                    and run["started_utc"] and selected["started_utc"] > run["started_utc"]
                    and selected["dependency_sha256"] == run["dependency_sha256"]):
                reconciled.append({"run": run, "resolved_by": selected["result"],
                                   "resolved_by_receipt_sha256": selected["receipt_sha256"],
                                   "reason": "The expected temporal violation preceded diagnostic OOM; a later matching receipt completed its diagnostic."})
            else:
                conflicts.append(run)
        if conflicts:
            selected = None
        rows.append({"case": case, "selected": selected,
                     "disposition": "conflicting_result" if conflicts else "selected" if selected else "missing",
                     "reconciled_diagnostics": reconciled,
                     "other_current_runs": [{k: r[k] for k in ("result", "status", "started_utc")}
                                            for r in runs if r is not selected]})
    # The owner may be editing while this scan runs. Never label mixed revisions current.
    relevant = {"check.py": current["check.py"]}
    for row in rows:
        if row["selected"]:
            relevant.update(row["selected"]["dependency_sha256"])
            relevant[row["selected"]["runner_file"]] = row["selected"]["runner_sha256"]
    if any(sha((spec / name).read_bytes()) != digest for name, digest in relevant.items()):
        raise ValueError("Selected inputs changed during collection; rerun against a stable revision")
    return {"schema": 1, "method": "Parsed/semantic import agreement + selected config + runner + pinned jar + raw outcome",
            "runner_sha256": current["check.py"], "jar_sha256": pin,
            "log_integrity": "Legacy receipts have no producer-recorded log digest. Collection cross-checks both module lists and the outcome, then hashes the retained log; this does not authenticate historical edits to that log.",
            "limitations": "Finite configured instances only. Authored schedules, abstraction and fault assumptions remain in the family reports. Case counts are not architectural coverage or implementation correctness.",
            "search_roots": [str(p.resolve()) for p in searches], "cases": rows,
            "rejected_receipts": dict(sorted(rejected.items()))}


def write_compact(report: dict, output: Path, catalogs: dict[str, str], collector_hash: str) -> None:
    sources = {"check.py": report["runner_sha256"]}
    rows = []
    for row in sorted(report["cases"], key=lambda r: r["case"]["name"]):
        case, selected = row["case"], row["selected"] or {}
        if selected:
            incoming = [*selected["dependency_sha256"].items(),
                        (selected.get("runner_file", "check.py"), selected["runner_sha256"])]
            for name, digest in incoming:
                if name in sources and sources[name] != digest:
                    raise ValueError("Contradictory selected input hash: " + name)
                sources[name] = digest
            if selected["jar_sha256"] != report["jar_sha256"]:
                raise ValueError("Contradictory selected pinned jar hash")
        states = selected.get("states", {})
        rows.append({"case": case["name"], "module": case["module"], "config": case["config"],
                     "status": selected.get("status", row["disposition"]),
                     "distinct": states.get("distinct", ""), "generated": states.get("generated", ""),
                     "depth": selected.get("depth", ""), "elapsed_seconds": selected.get("elapsed_seconds", ""),
                     "runner": selected.get("runner_file", ""), "recovered_states": selected.get("recovered_states", ""),
                     "workers": selected.get("workers", case.get("workers", 1)),
                     "heap": selected.get("heap", case.get("heap", "512m")),
                     "receipt_sha256": selected.get("receipt_sha256", ""),
                     "log_sha256": selected.get("log_sha256", "")})
    with (output / "checks.csv").open("w", newline="") as stream:
        columns = ("case", "module", "config", "status", "distinct", "generated", "depth",
                   "elapsed_seconds", "workers", "heap", "runner", "recovered_states", "receipt_sha256", "log_sha256")
        writer = csv.DictWriter(stream, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    write_json(output / "inputs.json", {"schema": 1,
               "source_sha256": dict(sorted(sources.items())), "jar_sha256": report["jar_sha256"],
               "collector_sha256": collector_hash, "catalog_sha256": dict(sorted(catalogs.items()))})


def bundle(report: dict, output: Path) -> None:
    retained = []
    for row in report["cases"]:
        if row["selected"]:
            destination = output / "runs" / row["case"]["name"]
            retained.append((row["selected"], destination))
            for index, entry in enumerate(row.get("reconciled_diagnostics", [])):
                retained.append((entry["run"], destination / "reconciled" / str(index)))
    size = 0
    for run, _ in retained:
        source = Path(run["result"]).parent
        size += (sum((source / name).stat().st_size for name in run["recovery_inputs_sha256"])
                 if "recovery_inputs_sha256" in run else
                 sum(p.stat().st_size for p in source.iterdir() if p.is_file()))
        size += sum((source / "sources" / name).stat().st_size
                    for name in run["dependency_sha256"])
    print(f"Bundling about {size / (1 << 30):.2f} GiB; original runs remain in place.", file=sys.stderr)
    for run, destination in retained:
        source = Path(run["result"]).parent
        destination.mkdir(parents=True)
        # Preserve raw logs (including counterexamples), progress and invocation.
        # Solver scratch/checkpoints and unparsed sibling models are not this bundle.
        if "recovery_inputs_sha256" in run:
            for name, digest in run["recovery_inputs_sha256"].items():
                data = (source / name).read_bytes()
                if sha(data) != digest:
                    raise ValueError("Selected recovery input changed before bundling: " + name)
                target = destination / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        else:
            for path in source.iterdir():
                if path.is_file() and not path.is_symlink():
                    shutil.copyfile(path, destination / path.name)
        for name, digest in run["dependency_sha256"].items():
            data = (source / "sources" / name).read_bytes()
            if sha(data) != digest:
                raise ValueError("Selected evidence changed before bundling: " + name)
            target = destination / "sources" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        for path in (source / "sources").glob("*_TTrace*"):
            if path.is_file() and not path.is_symlink():
                shutil.copyfile(path, destination / "sources" / path.name)
        receipt_name = run.get("receipt_name", "result.json")
        if (sha((destination / receipt_name).read_bytes()) != run["receipt_sha256"] or
                sha((destination / "tlc.log").read_bytes()) != run["log_sha256"] or
                sha((destination / run.get("runner_file", "check.py")).read_bytes()) != run["runner_sha256"] or
                sha((destination / "tool-source.json").read_bytes()) != run["tool_source_sha256"]):
            raise ValueError("Selected receipt/log/tool changed before bundling")
        run["bundle_result"] = (destination / receipt_name).relative_to(output).as_posix()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=SPEC / "cases.json")
    parser.add_argument("--search", type=Path, action="append", help="Repeat to search other run roots")
    parser.add_argument("--output", type=Path, required=True, help="New ignored output directory")
    parser.add_argument("--bundle", action="store_true", help="Copy selected input closure, raw logs and receipts")
    args = parser.parse_args()
    cases, catalog_sources = read_catalog(args.manifest)
    collector_hash = sha(Path(__file__).read_bytes())
    output = args.output.resolve()
    if output.is_relative_to(SPEC):
        parser.error("Keep generated evidence outside the model directory")
    if output.exists():
        parser.error("Use a new output directory; earlier selections remain evidence of their snapshot")
    report = collect(cases, args.search or SEARCH)
    output.mkdir(parents=True, exist_ok=False)
    if args.bundle:
        bundle(report, output)
        reference = output / "reference"
        reference.mkdir()
        for path in SPEC.glob("*.md"):
            shutil.copyfile(path, reference / path.name)
    write_json(output / "selection.json", report)
    catalogs = {os.path.relpath(path, args.manifest.resolve().parent): sha(data)
                for path, data in catalog_sources.items()}
    if sha(Path(__file__).read_bytes()) != collector_hash:
        raise ValueError("Collector changed during collection; rerun against a stable revision")
    write_compact(report, output, catalogs, collector_hash)
    missing = [r["case"] for r in report["cases"] if not r["selected"]]
    write_json(output / "missing.json", {"schema": 1, "cases": missing})
    counts = Counter(r["selected"]["status"] if r["selected"] else r["disposition"] for r in report["cases"])
    lines = ["# Selected Orbital checks", "", report["limitations"], "",
             "; ".join(f"{name}: {count}" for name, count in sorted(counts.items())), "",
             "Elapsed time is for the selected attempt; recovered states belong to its checkpoint, not work repeated in that time.", "",
             "| Case | Disposition | States | Recovered states | Attempt seconds |", "| --- | --- | ---: | ---: | ---: |"]
    for row in report["cases"]:
        selected = row["selected"] or {}
        lines.append(f"| {row['case']['name']} | {selected.get('status', row['disposition'])} | "
                     f"{selected.get('states', {}).get('distinct', '')} | {selected.get('recovered_states', '')} | {selected.get('elapsed_seconds', '')} |")
    (output / "SUMMARY.md").write_text("\n".join(lines) + "\n")
    print(json.dumps({"selection": str(output / "selection.json"), "counts": counts, "missing": len(missing)}))
    return 1 if missing else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError) as error:
        print(f"evidence.py: {error}", file=sys.stderr)
        raise SystemExit(2)
