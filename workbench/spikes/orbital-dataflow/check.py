"""Run this spike's focused checks and reproduce its small retained comparisons."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent
EVIDENCE = ROOT / "evidence"
PROBES = {"exchange": "exchanges", "progress": "progress",
          "resource": "resources", "lineage": "lineage"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    return {path.name: digest(path) for path in sorted(ROOT.glob("*.py"))}


def run_probe(name, destination):
    subprocess.run([sys.executable, str(ROOT / (name + "_probe.py")),
                    "--output", str(destination)], check=True)


def verify_manifest():
    manifest = json.loads((EVIDENCE / "manifest.json").read_text())
    if manifest["sources"] != source_hashes():
        raise AssertionError("executable source hashes differ from retained run")
    for name, expected in manifest["artifacts"].items():
        if digest(EVIDENCE / name) != expected:
            raise AssertionError("retained artifact hash mismatch: " + name)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--retain", action="store_true", help="regenerate the selected evidence and manifest")
    group.add_argument("--verify-only", action="store_true", help="check hashes without rerunning probes/tests")
    args = parser.parse_args()
    if args.verify_only:
        verify_manifest()
        print("Verified all executable and retained-evidence hashes.")
        return
    before = source_hashes()
    if args.retain:
        EVIDENCE.mkdir(exist_ok=True)
        for probe, artifact in PROBES.items():
            run_probe(probe, EVIDENCE / (artifact + ".json"))
    else:
        manifest = verify_manifest()
        if manifest["python"] != platform.python_version():
            raise AssertionError("use retained Python " + manifest["python"] + " for exact reproduction")
    for probe in PROBES:
        subprocess.run([sys.executable, str(ROOT / ("check_" + probe + ".py"))], check=True)
    if not args.retain:
        with tempfile.TemporaryDirectory(prefix="orbital-dataflow-") as directory:
            for probe, artifact in PROBES.items():
                output = Path(directory) / (artifact + ".json")
                run_probe(probe, output)
                if json.loads(output.read_text()) != json.loads((EVIDENCE / output.name).read_text()):
                    raise AssertionError("evidence reproduction differs: " + artifact)
    if before != source_hashes():
        raise AssertionError("source changed during the run")
    if args.retain:
        manifest = {"python": platform.python_version(), "sources": before,
                    "artifacts": {name + ".json": digest(EVIDENCE / (name + ".json"))
                                  for name in PROBES.values()},
                    "selection": "All authored comparative cases, including rejected/unfinished/invalid controls; seeded oracle checks are regenerated."}
        (EVIDENCE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("All focused checks passed; source hashes stable and selected evidence " +
          ("retained." if args.retain else "reproduced."))


if __name__ == "__main__":
    main()
