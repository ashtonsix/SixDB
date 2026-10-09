"""Retain bounded local timing/check and compile-only codegen comparisons."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
from codegen import inspect, short_name


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run = args.run.resolve()
    root = Path(__file__).resolve().parents[4]
    inputs = [run / ("selected-" + label + ".jsonl") for label in ("small", "large")]
    checks = {label: json.loads((run / name).read_text()) for label, name in
              (("native", "check.json"), ("asan_ubsan_always_uar", "sanitize-check.json"))}
    assert all(entry["passed"] and entry["matched_cases"] == 96 for entry in checks.values())
    cells = []
    for path in inputs:
        groups = defaultdict(list)
        for line in path.read_text().splitlines():
            row = json.loads(line)
            groups[(row["mode"], row["group_width"], row["hints"])].append(row)
        assert len(groups) == 24
        for key, rows in groups.items():
            assert len(rows) == 7 and sorted(row["rep"] for row in rows) == list(range(7))
            for field in ("reads", "hint_requests", "checksum", "touched_rows", "bytes", "jobs"):
                assert len({row[field] for row in rows}) == 1
            samples = [row["ns_per_read"] for row in rows]
            cells.append({field: rows[0][field] for field in
                          ("mode", "rows", "bytes", "touched_rows", "group_width", "active_width",
                           "hints", "jobs", "reads", "hint_requests", "checksum")} |
                         {"ns_per_read_samples": samples, "median_ns_per_read": statistics.median(samples),
                          "min_ns_per_read": min(samples), "max_ns_per_read": max(samples)})
        # Every scheduling/hint control sees the same logical work and results.
        all_rows = [row for rows in groups.values() for row in rows]
        for field in ("reads", "checksum", "touched_rows", "jobs"):
            assert len({row[field] for row in all_rows}) == 1

    codegen_path = run / "codegen-report/summary.json"
    codegen = json.loads(codegen_path.read_text())
    source_files = {str(path.relative_to(root)): sha(path) for path in
                    sorted((root / "workbench/spikes/call-boundaries/access-interleaving").glob("*"))
                    if path.is_file() and path.suffix in (".cpp", ".h", ".py", ".txt")}
    # The built target's Ninja dependency database covers actual runtime TUs and
    # their project headers. No whole-repository capture or index mutation.
    native = run / "native"
    cache = (native / "CMakeCache.txt").read_text().splitlines()
    source_root = Path(next(line.split("=", 1)[1] for line in cache
                            if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")))
    deps = subprocess.check_output(["ninja", "-C", str(native), "-t", "deps"], text=True)
    project_inputs = set()
    for line in deps.splitlines():
        if line.startswith("    "):
            path = Path(line.strip())
            try:
                name = path.relative_to(source_root)
            except ValueError:
                continue
            if path.is_file():
                project_inputs.add(name)
    source_files.update({str(path): sha(root / path) for path in sorted(project_inputs)})
    compile_receipt = json.loads((run / "codegen-report/receipt.json").read_text())
    for target in compile_receipt["targets"].values():
        for name, wanted in target["dependency_sha256"].items():
            path = Path(name)
            if path.is_relative_to(root):
                assert sha(path) == wanted, "codegen input changed: " + name
    binary = native / "workbench/spikes/call-boundaries/access-interleaving/call_boundary_access_interleaving"
    native_static_dir = run / "native-static"
    native_static_dir.mkdir(exist_ok=True)
    kernel_object = native / ("workbench/spikes/call-boundaries/access-interleaving/"
                              "CMakeFiles/call_boundary_access_interleaving.dir/kernels.cpp.o")
    report = inspect(kernel_object, "arm", native_static_dir / "kernels.o")
    native_static = {"object_sha256": sha(kernel_object), "text_bytes": report["text_bytes"],
                     "qualification": "Exact local AArch64 baseline/generic timing object; static sites.",
                     "functions": [{"name": short_name(f["name"]), "role": f["role"],
                                    "bytes": f["symbol_bytes"], "instructions": f["static_instruction_sites"],
                                    "ordinary_calls": len(f["ordinary_call_sites"]),
                                    "stack_memory_sites": len(f["stack_memory_sites"]),
                                    "vector_stack_memory_sites": len(f["vector_stack_memory_sites"])}
                                   for f in report["functions"]]}
    native_flags = {line.split("=", 1)[0]: line.split("=", 1)[1] for line in cache if
                    line.startswith(("CMAKE_CXX_FLAGS:", "CMAKE_CXX_FLAGS_RELEASE:", "SIXDB_MARCH:", "SIXDB_TUNE:"))}
    summary = {"date": "2026-10-09", "checks": checks,
               "environment": {"machine": subprocess.check_output(["uname", "-m"], text=True).strip(),
                               "kind": "local Apple AArch64 OrbStack VM", "cpu_affinity": [0],
                               "native_flags": native_flags},
               "binary_sha256": sha(binary), "source_files_sha256": source_files,
               "raw_inputs_sha256": {str(path.relative_to(root)): sha(path) for path in inputs},
               "timings": cells, "codegen": codegen,
               "native_static": native_static,
               "limits": [
                   "Version/Representation selection and publication are fixture scaffolding; no Objects runtime.",
                   "Timings include frame reset, initial hints, group entry/admission and complete drain.",
                   "Construction, qualification, expected-result oracle and Ring allocation are outside timing.",
                   "Fixed groups with ragged completion; neither manual nor Ring immediately refills vacated lanes.",
                   "Sequential mode order, seven repetitions, no statistical confidence or p99 claim.",
                   "Starts vary over 16 precomputed batches; the same set is reused across samples/modes.",
                   "Distinct logical rows touched is reported; allocation size does not establish cache residency.",
                   "Local VM timing is not Zen5/V2 hardware proof; no PMU stalls or IPC were measured.",
                   "Static instruction/stack sites enumerate alternate paths, not executed cost or cycles.",
                   "Native source association is provisional: project hashes were collected post-run, not as a build-time identity for every timed-binary input.",
                   "V2/Zen5 codegen objects have independently verified transitive input manifests; no frozen whole-repository capture."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(f"Retained {len(cells)} timing cells; native/sanitizer histories and V2/Zen5 codegen")


if __name__ == "__main__":
    main()
