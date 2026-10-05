#!/usr/bin/env python3
"""Recover the measured pre-tuning implementation, compiling in its own namespace."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--source", type=Path, help="already recovered source.tar.gz")
    args = parser.parse_args()
    study = Path(__file__).resolve().parent
    args.destination.mkdir(parents=True, exist_ok=True)
    archive = args.source
    if archive is None:
        recovery = args.destination / "recovered"
        if not (recovery / "source.tar.gz").exists():
            subprocess.run([sys.executable, str(study.parent.parent / "tools/artifacts.py"),
                            "fetch", str(study / "tuning-baseline.json"), str(recovery),
                            "--file", "source.tar.gz"], check=True, stdout=sys.stderr)
        archive = recovery / "source.tar.gz"
    frozen = args.destination / "frozen"
    frozen.mkdir(exist_ok=True)
    receipt = dict(reference=json.loads((study / "tuning-baseline.json").read_text()),
                   source_tar_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(), files={})
    with tarfile.open(archive) as source:
        receipt["shared_sources"] = {}
        for name in ("arena.h", "grouped_bench.cpp"):
            original = source.extractfile(f"workbench/spikes/memory-descriptors/{name}").read()
            if original != (study / name).read_bytes():
                raise RuntimeError(f"shared baseline source changed: {name}; review comparison before proceeding")
            receipt["shared_sources"][name] = hashlib.sha256(original).hexdigest()
        for name in ("grouped.h", "grouped.cpp", "grouped_probe.cpp", "grouped_bench.cpp"):
            member = f"workbench/spikes/memory-descriptors/{name}"
            data = source.extractfile(member).read()
            text = data.decode().replace("namespace descriptors {", "namespace frozen_descriptors {\n"
                "using descriptors::Head;\nusing descriptors::native;\n"
                "using descriptors::arena_id;\nusing descriptors::tag_mask;\n")
            text = text.replace("using namespace descriptors;", "using namespace frozen_descriptors;")
            transformed = text.encode()
            (frozen / name).write_bytes(transformed)
            receipt["files"][name] = dict(original_sha256=hashlib.sha256(data).hexdigest(),
                                         namespaced_sha256=hashlib.sha256(transformed).hexdigest())
    print(json.dumps(receipt, indent=2))

if __name__ == "__main__":
    main()
