#!/usr/bin/env python3
"""Recover the exact pre-U-split public headers/TUs into ignored capture input."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("output", type=Path)
p.add_argument("--revision", default="35f05633")
args = p.parse_args()
revision = subprocess.check_output(["git", "rev-parse", args.revision], text=True).strip()
paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", revision, "ikea/include/ikea", "ikea/src/boundlist", "ikea/src/streampack"], text=True).splitlines()
for path in paths:
    if path.startswith("ikea/include/ikea/") and not any(path == f"ikea/include/ikea/{m}.h" or path.startswith(f"ikea/include/ikea/{m}/") for m in ("boundlist", "streampack")):
        continue
    target = args.output / path.removeprefix("ikea/")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(subprocess.check_output(["git", "show", f"{revision}:{path}"]))
(args.output / "revision.txt").write_text(revision + "\n")
print(revision)
