#!/usr/bin/env python3
"""Retain raw layout inputs and derivation with the original worker recovery ref."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

worker, destination = map(Path, sys.argv[1:])
results = worker / "results"
inputs, analysis = destination / "inputs", destination / "analysis"
inputs.mkdir(parents=True, exist_ok=True)
analysis.mkdir(exist_ok=True)
for path in [*results.glob("*/layout-*.csv"), *results.glob("*/bulk-*.csv"), *results.glob("*/growth-[01].csv")]:
    target = inputs / path.parent.name / path.name
    target.parent.mkdir(exist_ok=True)
    shutil.copy2(path, target)
for path in [*results.glob("*/checks.txt"), *results.glob("*/streampack_layout_explicit*-checks.txt")]:
    assert "layout checks passed" in path.read_text(), path
    shutil.copy2(path, inputs / path.parent.name / path.name)
for path in results.glob("*/binaries.sha256"):
    shutil.copy2(path, inputs / path.parent.name / path.name)
for name in ("summarize_layout.py", "summarize_layout_growth.py", "package_layout.py"):
    shutil.copy2(Path(__file__).with_name(name), analysis / name)
subprocess.run([sys.executable, str(analysis / "summarize_layout.py"), str(inputs), str(destination)], check=True)
if list(inputs.glob("*/bulk-*.csv")):
    subprocess.run([sys.executable, str(analysis / "summarize_layout.py"), str(inputs), str(destination),
                    "--prefix", "bulk", "--output-name", "bulk-summary.csv"], check=True)
if list(inputs.glob("*/growth-[01].csv")):
    for path in results.glob("*/growth-checks.txt"):
        assert "growth checks passed" in path.read_text(), path
        shutil.copy2(path, inputs / path.parent.name / path.name)
    subprocess.run([sys.executable, str(analysis / "summarize_layout_growth.py"), str(inputs), str(destination)], check=True)
shutil.copy2(worker / "artifact.json", destination / "worker-artifact.json")
for name in ("host.json", "job.json", "worker-result.json"):
    shutil.copy2(results / name, inputs / name)
host = json.loads((results / "host.json").read_text())
job = json.loads((results / "job.json").read_text())
status = json.loads((results / "worker-result.json").read_text())
assert status["state"] == "complete", status
(destination / "context.json").write_text(json.dumps({
    "worker": host["worker"], "instance_type": host["instance"]["instanceType"],
    "compiler": host.get("compiler"), "source": job["source"], "source_commit": job["source_commit"],
    "worker_result": status,
    "derivation": "python3 analysis/summarize_layout.py inputs REGENERATED_DIRECTORY; if bulk inputs are present, repeat with --prefix bulk --output-name bulk-summary.csv",
    "summary_script_sha256": hashlib.sha256((analysis / "summarize_layout.py").read_bytes()).hexdigest(),
    "provenance": "Derived bundle contains exact CSV inputs and analysis. worker-artifact.json recovers original sources, binaries, disassembly and logs. Summaries generated after collection."
}, indent=2) + "\n")
