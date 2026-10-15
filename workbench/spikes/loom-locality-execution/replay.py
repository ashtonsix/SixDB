#!/usr/bin/env python3
"""Qualify recovered bytes with current tools, without executing captured code."""
import argparse
import hashlib
import json
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("--followup", action="store_true")
parser.add_argument("--source-output", type=Path, help="recover the full verified source into a new directory for consumer input")
args = parser.parse_args()
member = "workbench/spikes/hashtable-design/staged.cpp"
manifest = json.loads((args.root / "source-manifest.json").read_text())
with tempfile.TemporaryDirectory(prefix="loom-replay-") as temporary:
    source_root = args.source_output or Path(temporary)
    if args.source_output:
        if source_root.exists():
            parser.error("source output must be a new directory")
        source_root.mkdir(parents=True)
    full_tree = args.followup or args.source_output is not None
    seen = set()
    with tarfile.open(args.root / "source.tar.gz", "r:gz") as archive:
        for item in archive:
            if not full_tree and item.name != member:
                continue
            name = PurePosixPath(item.name)
            if (not item.isfile() or item.name not in manifest or item.name in seen or
                    name.is_absolute() or ".." in name.parts or str(name) != item.name):
                parser.error("captured source member invalid or ambiguous")
            source = archive.extractfile(item).read()
            if hashlib.sha256(source).hexdigest() != manifest[item.name]:
                parser.error("captured source differs from manifest")
            path = source_root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source)
            seen.add(item.name)
    if seen != (set(manifest) if full_tree else {member}):
        parser.error("captured source inventory incomplete")
    script = Path(__file__).with_name("followup_report.py" if args.followup else "report.py")
    subprocess.run([sys.executable, "-B", str(script), str(args.root), "--qualify",
                    "--source-root", str(source_root)], check=True)
