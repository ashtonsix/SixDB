#!/usr/bin/env python3
"""Captured, serial resident comparison of the actual versioned consumers."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import statistics
import sys
from pathlib import Path

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[3]
sys.path.insert(0, str(ROOT / "workbench/tools"))
from experiment import Run


def panel():
    # A bounded diagnostic, not a hardware/workload search. One larger fixture
    # exposes body search cost; batch1/64 exposes selection amortization.
    for rows, objects, families in [
        (1024, 1, ["point-hit", "point-miss"]),
        (32768, 4, ["point-hit", "dependent-16", "scan-16"]),
    ]:
        for family in families:
            for batch in ([1] if family == "point-miss" else [1, 64]):
                yield dict(rows=rows, objects=objects, workload=family, batch=batch,
                           ops=32771, backends=["orbital", "borrowed", "lmdb", "direct"])
    for batch in [1, 64]:
        yield dict(rows=1024, objects=1, workload="point-hit", batch=batch,
                   ops=32771, backends=["row-lmdb"])
    for rows in [4096, 32768]:
        for batch in [1, 64]:
            yield dict(rows=rows, objects=4, workload="point-update", batch=batch,
                       ops=131, backends=["orbital", "lmdb"])
    for initial in ["rows", "columns"]:
        yield dict(rows=4096, objects=4, workload="convert", batch=1,
                   ops=16, backends=["orbital"], **{"from": initial})


def csv_file(path, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize(output):
    rows, groups, parity = [], {}, {}
    for path in sorted(output.glob("trial-*.json")):
        result = json.loads(path.read_text())
        if not result["passed"]:
            raise ValueError(f"qualification failed: {path}")
        stem = path.stem.split("-")
        repeat = int(stem[2])
        interval = result["interval"]
        expected_scopes = (result["operations"] + result["batch"] - 1) // result["batch"]
        if result["backend"] != "direct" and interval["snapshot_scopes"] != expected_scopes:
            raise ValueError(f"snapshot grouping differs: {path}")
        if interval["batch_samples"] != (expected_scopes if result["mode"] == "latency" else 0):
            raise ValueError(f"clock replay mode differs: {path}")
        key = (result["workload"], result["rows"], result["objects"], result["batch"], result["from"])
        identity = (result["trace_fingerprint"], result["outcome_digest"], interval["logical_requests"],
                    interval["logical_rows"], result["seed"])
        if key in parity and parity[key] != identity:
            raise ValueError(f"matched native outcomes differ: {path}")
        parity[key] = identity
        base = dict(workload=key[0], rows=key[1], objects=key[2], batch=key[3], source_format=key[4],
                    backend=result["backend"], mode=result["mode"], repeat=repeat,
                    comparison="conversion" if key[0] == "convert" else
                    "contextual-row-key" if result["backend"] == "row-lmdb" else
                    "prequalified-native" if result["backend"] == "direct" else "common-tuple",
                    wall_ns=interval["wall_ns"], caller_cpu_ns=interval["caller_cpu_ns"],
                    ns_per_request=interval["wall_ns"]/result["operations"],
                    snapshot_scopes=interval["snapshot_scopes"], bindings=result["resolved_bindings"],
                    logical_rows=interval["logical_rows"],
                    batch_p50_ns=interval["batch_makespan_ns"]["p50"],
                    batch_p99_ns=interval["batch_makespan_ns"]["p99"],
                    copied_bytes=result["costs"]["copied_bytes"],
                    source_bindings=result["update_preparation"]["source_bindings"],
                    dynamic_shape_preparations=result["update_preparation"]["dynamic_shape_preparations"],
                    requested_body_bytes=result["costs"]["requested_body_bytes"],
                    scratch_bound_bytes=result["costs"]["scratch_bound_bytes"],
                    release_and_drain_ns=result["release_and_drain"]["wall_ns"])
        for label, field in [("initial", "footprint_initial"), ("held", "footprint_after_interval"),
                             ("released", "footprint_after_reader_release")]:
            f = result.get(field, {})
            base.update({f"{label}_arena_charge": f.get("arena_charged_bytes", 0),
                         f"{label}_metadata_bytes": f.get("metadata_bytes", 0),
                         f"{label}_versions": f.get("versions", 0),
                         f"{label}_file_used_bytes": f.get("file_used_bytes", 0)})
        rows.append(base)
        group_key = key + (result["backend"], result["mode"])
        groups.setdefault(group_key, []).append(base)
    csv_file(output/"measurements.csv", rows)
    summary = []
    for key, values in groups.items():
        summary.append(dict(workload=key[0], rows=key[1], objects=key[2], batch=key[3],
                            source_format=key[4], backend=key[5], mode=key[6],
                            repetitions=len(values),
                            median_ns_per_request=statistics.median(x["ns_per_request"] for x in values),
                            min_ns_per_request=min(x["ns_per_request"] for x in values),
                            max_ns_per_request=max(x["ns_per_request"] for x in values),
                            median_batch_p99_ns=statistics.median(x["batch_p99_ns"] for x in values)))
    csv_file(output/"summary.csv", summary)
    (output/"parity.json").write_text(json.dumps(dict(passed=True, trials=len(rows), matched_cases=len(parity),
        independent_cli_oracle=True, scopes_and_modes_checked=True, all_repetitions_retained=True), indent=2)+"\n")


def abi(output):
    wanted = ("::run_orbital_reads(", "::run_lmdb_reads(", "::run_direct_reads(",
              "::Orbital::cursor(", "::qualify_tuple(", "::ReadData::get(",
              "::read_operation<orbital_bench::versioned::TupleBody>",
              "::Store::read(", "::Store::prepare(", "::Store::install(", "::Store::drain(")
    sizes = {}
    for line in (output/"symbols.txt").read_text().splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) == 4 and any(x in fields[3] for x in wanted):
            sizes[fields[3]] = int(fields[1], 16)
    functions, name, instructions = [], None, []
    def flush():
        if name and any(x in name for x in wanted):
            functions.append(dict(symbol=name, code_bytes=sizes.get(name),
                prologue=instructions[:12],
                stack_adjustments=[x for x in instructions if
                    re.search(r"\bsub\s+sp,\s*sp,|\[sp,\s*#-\w+\]!", x)],
                call_targets=sorted(set(re.findall(r"\bbl\s+\S+\s+<(.*)>$", "\n".join(instructions), re.MULTILINE))),
                direct_memcpy_call=any("memcpy" in x and re.search(r"\bbl\b", x) for x in instructions)))
    for line in (output/"disassembly.txt").read_text().splitlines():
        match = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if match:
            flush(); name, instructions = match[1], []
        elif re.match(r"^\s+[0-9a-f]+:", line):
            instructions.append(line.strip())
    flush()
    if not any("::run_orbital_reads(" in x["symbol"] for x in functions):
        raise ValueError("actual executable caller missing from disassembly")
    with (output/"bin/orbital_versioned_performance_bench").open("rb") as binary:
        header = binary.read(20)
    if len(header) < 20 or header[:4] != b"\x7fELF" or header[5] not in [1, 2]:
        raise ValueError("captured executable is not a supported ELF file")
    machine = int.from_bytes(header[18:20], "little" if header[5] == 1 else "big")
    architecture = {183: "aarch64", 62: "x86_64"}.get(machine, f"ELF-machine-{machine}")
    (output/"abi.json").write_text(json.dumps(dict(architecture=architecture,
        method="exact measured executable, separate TUs, no LTO; structural only",
        limitation="stack traffic is not evidence of forwarding stalls; no model cycles",
        functions=functions), indent=2)+"\n")


def sanitizer_summary(output):
    path = output/"sanitizers/qualification.json"
    if not path.exists():
        return
    receipt = json.loads(path.read_text())
    source_digest = hashlib.sha256(json.dumps(receipt["source_sha256"], sort_keys=True).encode()).hexdigest()
    summary = dict(passed=receipt["passed"], processes=len(receipt["commands"]),
        source_sha256_digest=source_digest, same_compiled_source_as_native_capture=True,
        existing_db_unchanged=receipt["existing_db_unchanged"],
        instrumentation="ASan/UBSan; always-use-after-return; frame pointers; separate build",
        environment=receipt["sanitizer_env"], binary_sha256=receipt["binary_sha256"],
        full_receipt_in_archive="sanitizers/qualification.json",
        scope="8 Objects, native caller modes and tails, retained update/conversion, invalid options, reused DB refusal")
    (output/"sanitizer-summary.json").write_text(json.dumps(summary, indent=2)+"\n")


def report(output):
    summarize(output)
    abi(output)
    sanitizer_summary(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, default=ROOT/"build/workspaces/versioned-performance")
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--sanitizer-qualification", type=Path,
                        help="Final separately qualified CLI receipt directory (same source cut)")
    args = parser.parse_args()
    if not hasattr(os, "sched_getaffinity"):
        parser.error("run on Linux (orb -m ubuntu from macOS)")
    allowed = os.sched_getaffinity(0)
    if args.cpu not in allowed or args.repetitions < 1:
        parser.error("require an allowed CPU and positive repetitions")
    output, workspace = args.output.resolve(), args.workspace.resolve()
    if not output.is_relative_to(ROOT/"build") or output.exists():
        parser.error("output must be a new directory under build/")
    run = Run(ROOT, output, dict(cpu=args.cpu, allowed_cpus=sorted(allowed),
                                repetitions=args.repetitions, panel=list(panel())), workspace=workspace)
    error = None
    try:
        run.step("git-head", ["git", "rev-parse", "HEAD"])
        run.step("git-status", ["git", "status", "--short"])
        run.step("host", ["lscpu"])
        run.step("compiler", ["clang++-21", "--version"])
        run.step("configure", ["cmake", "-S", str(run.source_root), "-B", str(run.build_dir), "-G", "Ninja",
                              "-DCMAKE_BUILD_TYPE=Release", "-DSIXDB_BENCHMARKS=", "-DSIXDB_SPIKES=orbital-mvcc",
                              "-DSIXDB_MARCH=", "-DSIXDB_TUNE=generic"])
        targets = ["orbital_versioned_performance_bench", "orbital_versioned_performance_check",
                   "orbital_versioned_lmdb_check", "orbital_versioned_body_check"]
        run.step("build", ["cmake", "--build", str(run.build_dir), "--target", *targets, "-j2"])
        for name in ["CMakeCache.txt", "compile_commands.json"]:
            shutil.copy2(run.build_dir/name, output/name)
        binary_root = run.build_dir/STUDY.relative_to(ROOT)
        (output/"bin").mkdir()
        hashes = {}
        for target in targets:
            shutil.copy2(binary_root/target, output/"bin"/target)
            hashes[target] = hashlib.sha256((output/"bin"/target).read_bytes()).hexdigest()
        (output/"binary-identity.json").write_text(json.dumps(hashes, indent=2)+"\n")
        run.step("linked-libraries", ["ldd", str(output/"bin"/targets[0])])
        run.step("lmdb-version", ["dpkg-query", "-W", "liblmdb0", "liblmdb-dev"])
        if args.sanitizer_qualification:
            directory = args.sanitizer_qualification.resolve()
            receipt = json.loads((directory/"qualification.json").read_text())
            if not receipt["passed"] or any(run.receipt["source_files_sha256"].get(name) != digest
                for name, digest in receipt["source_sha256"].items()):
                raise ValueError("sanitizer qualification does not match captured source")
            (output/"sanitizers").mkdir()
            for path in directory.iterdir():
                if path.is_file():
                    shutil.copy2(path, output/"sanitizers"/path.name)
            (output/"sanitized-bin").mkdir()
            sanitized_root = directory.parents[1]/STUDY.relative_to(ROOT)
            for target, digest in receipt["binary_sha256"].items():
                binary = sanitized_root/target
                if hashlib.sha256(binary.read_bytes()).hexdigest() != digest:
                    raise ValueError("sanitized executable changed after qualification")
                shutil.copy2(binary, output/"sanitized-bin"/target)
            qualification_flags = directory.parents[1]
            for filename in ["CMakeCache.txt", "compile_commands.json"]:
                shutil.copy2(qualification_flags/filename, output/"sanitizers"/filename)
        os.sched_setaffinity(0, {args.cpu})
        run.receipt["measurement_affinity"] = sorted(os.sched_getaffinity(0))
        qualification = {}
        for target in targets[1:]:
            name = target.removeprefix("orbital_versioned_")
            db = output/f"qualification-{name}-db"
            run.step(name, [str(output/"bin"/target), str(db)], f"{name}.json")
            qualification[name] = json.loads((output/f"{name}.json").read_text())
            shutil.rmtree(db)
        (output/"qualification.json").write_text(json.dumps(qualification, indent=2)+"\n")
        for case_id, case in enumerate(panel()):
            for repeat in range(args.repetitions):
                backends = case["backends"]
                backends = backends[repeat % len(backends):] + backends[:repeat % len(backends)]
                for mode in ["throughput", "latency"]:
                    for backend in backends:
                        name = f"trial-{case_id:02d}-{repeat}-{mode}-{backend}"
                        db = output/f"{name}-db"
                        argv = [str(output/"bin"/targets[0]), "--backend", backend, "--mode", mode,
                                "--seed", "8132", "--bytes", str(256*1024*1024), "--db", str(db)]
                        for option in ["rows", "objects", "workload", "batch", "ops", "from"]:
                            if option in case:
                                argv += [f"--{option}", str(case[option])]
                        run.step(name, argv, f"{name}.json")
                        result = json.loads((output/f"{name}.json").read_text())
                        if not result["passed"]:
                            raise ValueError(f"trial failed: {name}")
                        if db.exists():
                            shutil.rmtree(db)  # Only this invocation's new, validated fixture.
        summarize(output)
        run.step("symbols", ["llvm-nm-21", "-S", "--size-sort", "-C", str(output/"bin"/targets[0])], "symbols.txt")
        run.step("disassembly", ["llvm-objdump-21", "-d", "-C", "--no-show-raw-insn",
                               str(output/"bin"/targets[0])], "disassembly.txt")
        abi(output)
        selected = ["measurements.csv", "summary.csv", "parity.json", "qualification.json", "binary-identity.json", "abi.json"]
        if args.sanitizer_qualification:
            sanitizer_summary(output)
            selected += ["sanitizer-summary.json"]
        run.compact(selected, [])
    except Exception as exc:
        error = exc
    finally:
        os.sched_setaffinity(0, allowed)
        error = run.finish(error)
    if error:
        raise error
    print(f"Complete: {output}", flush=True)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--summarize":
        summarize(Path(sys.argv[2]))
    elif len(sys.argv) == 3 and sys.argv[1] == "--abi":
        abi(Path(sys.argv[2]))
    elif len(sys.argv) == 3 and sys.argv[1] == "--report":
        report(Path(sys.argv[2]))
    else:
        main()
