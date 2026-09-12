"""Run the small NEON screen sequentially on one Linux CPU; retain compact samples."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=Path("build/tuple-layout/neon"))
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--cpu", type=int, default=2)
parser.add_argument("--filter", default="edge/")
args = parser.parse_args()
if args.cpu not in os.sched_getaffinity(0):
    parser.error("CPU is outside the process affinity")
args.output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
binary = (args.build / "workbench/spikes/tuple-layout/tuple_layout_bench").resolve()
check = binary.with_name("tuple_layout_check")

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

sources = {name: digest(source / name) for name in
           ["edge.h", "edge.cpp", "check.cpp", "bench.cpp", "CMakeLists.txt", "screen.py"]}
binary_hash = digest(binary)
validation = subprocess.check_output([str(check)], text=True)
raw = (args.output / "samples.json").resolve()
command = ["taskset", "-c", str(args.cpu), str(binary),
           f"--benchmark_filter={args.filter}", f"--benchmark_out={raw}",
           "--benchmark_out_format=json", "--benchmark_enable_random_interleaving=false",
           "--benchmark_color=false"]
with (args.output / "run.log").open("w") as log:
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
assert digest(binary) == binary_hash, "Binary changed during the run"
assert all(digest(source / name) == value for name, value in sources.items()), "Sources changed during the run"
result = json.loads(raw.read_text())
groups = {}
for sample in result["benchmarks"]:
    if sample.get("run_type") != "iteration":
        continue
    if sample.get("error_occurred"):
        raise RuntimeError(sample)
    assert sample["time_unit"] == "ns"
    name = sample["run_name"]
    # The benchmark iteration processes 256 tuples. Preserve every repetition.
    groups.setdefault(name, []).append(sample["cpu_time"] / 256)
with (args.output / "samples.csv").open("w", newline="") as file:
    writer = csv.writer(file)
    writer.writerow(["case", "median_ns_tuple", "min_ns_tuple", "max_ns_tuple", "samples_ns_tuple"])
    for name, values in groups.items():
        writer.writerow([name, statistics.median(values), min(values), max(values), json.dumps(values)])
metadata = {
    "scope": "Local Apple-hosted AArch64 VM screen; no server CPU or transaction claim",
    "command": command, "validation": validation, "host": platform.platform(),
    "compiler": subprocess.check_output(["clang++-21", "--version"], text=True),
    "build_cache_sha256": digest(args.build / "CMakeCache.txt"),
    "compile_commands_sha256": digest(args.build / "compile_commands.json"),
    "source_sha256": sources, "binary_sha256": binary_hash,
    "google_benchmark_context": result["context"], "cases": len(groups),
}
(args.output / "receipt.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(f"{len(groups)} cases; samples and receipt: {args.output}")
