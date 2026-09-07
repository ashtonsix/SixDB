#!/usr/bin/env python3
"""Refresh the stable dev compilation database for explicitly active spikes."""

from __future__ import annotations

import argparse
import fcntl
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/clang/dev"


def active_spikes():
    cache = BUILD / "CMakeCache.txt"
    if not cache.exists():
        return set()
    match = re.search(r"^SIXDB_SPIKES:[^=]+=(.*)$", cache.read_text(), re.MULTILINE)
    return set(filter(None, match[1].split(";"))) if match else set()


def refresh(selected):
    # File API distinguishes a genuinely empty configuration from a stale
    # compile_commands.json left by CMake after its last source target disappears.
    query = BUILD / ".cmake/api/v1/query/client-sixdb-dev"
    query.mkdir(parents=True, exist_ok=True)
    (query / "codemodel-v2").touch()
    command = ["cmake", "--preset", "dev", "-DSIXDB_SPIKES=" + ";".join(sorted(selected))]
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode:
        return result.returncode
    reply = BUILD / ".cmake/api/v1/reply"
    index = json.loads(max(reply.glob("index-*.json")).read_text())
    model_file = index["reply"]["client-sixdb-dev"]["codemodel-v2"]["jsonFile"]
    model = json.loads((reply / model_file).read_text())
    has_sources = any(
        json.loads((reply / target["jsonFile"]).read_text()).get("compileGroups")
        for config in model["configurations"] for target in config["targets"]
    )
    database = BUILD / "compile_commands.json"
    if not has_sources:
        temporary = database.with_suffix(".tmp")
        temporary.write_text("[]\n")
        temporary.replace(database)
    if not database.exists():
        raise RuntimeError("CMake configured source targets but did not export their compilation database")
    print("Active spikes: " + (", ".join(sorted(selected)) or "(none)"))
    print(f"Editor compilation database: {database}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--add", action="append", default=[], help="Activate a spike; repeat as needed")
    parser.add_argument("--remove", action="append", default=[], help="Deactivate a spike; repeat as needed")
    parser.add_argument("--list", action="store_true", help="List current selection without configuring")
    args = parser.parse_args()
    if args.list and (args.add or args.remove):
        parser.error("--list cannot be combined with changes")
    if set(args.add) & set(args.remove):
        parser.error("a spike cannot be added and removed together")
    for name in args.add + args.remove:
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name):
            parser.error(f"invalid spike name: {name}")
    BUILD.parent.mkdir(parents=True, exist_ok=True)
    with (BUILD.parent / ".dev-configure.lock").open("a") as lock:
        # Concurrent workbench runs merge their selections under one lock.
        fcntl.flock(lock, fcntl.LOCK_EX)
        selected = active_spikes()
        if args.list:
            print("\n".join(sorted(selected)))
            return 0
        selected.difference_update(args.remove)
        selected.update(args.add)
        for name in selected:
            if not (ROOT / "workbench/spikes" / name / "CMakeLists.txt").is_file():
                parser.error(f"active spike {name!r} has no CMakeLists.txt; remove it with --remove {name}")
        return refresh(selected)


if __name__ == "__main__":
    raise SystemExit(main())
