#!/usr/bin/env python3
"""Archiveable derived summaries; original worker bytes keep their own receipt."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

worker, variant, destination = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
results = worker / "results"
source = results / variant
destination.mkdir(parents=True, exist_ok=True)
inputs, analysis = destination / "inputs", destination / "analysis"
inputs.mkdir(exist_ok=True)
analysis.mkdir(exist_ok=True)
for pattern in ("ring-*.csv", "hint-*.csv", "mutation-*.csv", "mvcc-*.csv",
                "single-*.csv", "prepared-*.csv", "phases-*.csv", "phases-*.txt"):
    for path in source.glob(pattern):
        shutil.copy2(path, inputs / path.name)
for name in ("summarize_composition.py", "package_composition.py"):
    shutil.copy2(Path(__file__).with_name(name), analysis / name)
subprocess.run([sys.executable, str(analysis / "summarize_composition.py"),
                str(inputs), str(destination)], check=True)
shutil.copy2(worker / "artifact.json", destination / "worker-artifact.json")
for name in ("host.json", "job.json", "worker-result.json"):
    shutil.copy2(results / name, inputs / name)
for name in ("binaries.sha256", "checks.txt", "references.txt"):
    if (source / name).exists():
        shutil.copy2(source / name, inputs / name)
host = json.loads((results / "host.json").read_text())
job = json.loads((results / "job.json").read_text())
status = json.loads((results / "worker-result.json").read_text())
assert status["state"] == "complete", status
(destination / "context.json").write_text(json.dumps({
    "worker": host["worker"], "instance_type": host["instance"]["instanceType"],
    "variant": variant, "compiler": host.get("compiler"), "source": job["source"],
    "source_commit": job["source_commit"], "worker_result": status,
    "derivation": "python3 analysis/summarize_composition.py inputs REGENERATED_DIRECTORY",
    "summary_script_sha256": hashlib.sha256((analysis / "summarize_composition.py").read_bytes()).hexdigest(),
    "provenance": "This derived bundle contains its exact CSV/phase inputs and analysis. worker-artifact.json recovers original sources, binaries, disassembly, logs and mappings; summaries were generated after worker collection."
}, indent=2) + "\n")
