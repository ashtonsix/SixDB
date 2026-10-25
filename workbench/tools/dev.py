#!/usr/bin/env python3
"""Refresh the stable dev compilation database for active spikes and recurring benchmark suites."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import re
import subprocess
from pathlib import Path
import storage

def checkout_root(path, home):
    # OrbStack exposes the same Linux home at both /home/... and the macOS
    # mount path. resolve() cannot collapse bind mounts. Prefer the native home
    # so clangd sees the same source paths as a Remote SSH editor, whichever
    # alias was used to launch this helper. Check identity, not path spelling.
    root = path.resolve()
    for parent in root.parents:
        if parent.samefile(home):
            return home / root.relative_to(parent)
    return root


ROOT = checkout_root(Path(__file__).resolve().parents[2], Path.home())
BUILD = ROOT / "build/clang/dev"


def active_selection(variable):
    cache = BUILD / "CMakeCache.txt"
    if not cache.exists():
        return set()
    match = re.search(rf"^{variable}:[^=]+=(.*)$", cache.read_text(), re.MULTILINE)
    return set(filter(None, match[1].split(";"))) if match else set()


def refresh(selected, benchmarks, *, log=None):
    storage.initialize(ROOT)
    # A half-written new target must not replace working editor commands or
    # poison the next refresh's cached selection.
    paths = [BUILD / "CMakeCache.txt", BUILD / "compile_commands.json",
             BUILD / "ikea/compile_commands.json"]
    previous = {path: path.read_bytes() if path.exists() else None for path in paths}
    # File API distinguishes a genuinely empty configuration from a stale
    # compile_commands.json left by CMake after its last source target disappears.
    query = BUILD / ".cmake/api/v1/query/client-sixdb-dev"
    query.mkdir(parents=True, exist_ok=True)
    (query / "codemodel-v2").touch()
    command = ["cmake", "--preset", "dev", "-DSIXDB_SPIKES=" + ";".join(sorted(selected)),
               "-DSIXDB_BENCHMARKS=" + ";".join(sorted(benchmarks))]
    result = subprocess.run(command, cwd=ROOT, stdout=log,
                            stderr=subprocess.STDOUT if log is not None else None)
    if result.returncode:
        for path, content in previous.items():
            if content is None:
                path.unlink(missing_ok=True)
            else:
                temporary = path.with_suffix('.restore')
                temporary.write_bytes(content)
                temporary.replace(path)
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
    # clangd's header inference prefers matching basenames, regardless of target
    # ownership. Give Ikea headers only Ikea commands to choose from.
    commands = json.loads(database.read_text())
    ikea_database = BUILD / "ikea/compile_commands.json"
    ikea_database.parent.mkdir(parents=True, exist_ok=True)
    temporary = ikea_database.with_suffix(".tmp")
    temporary.write_text(json.dumps([row for row in commands
        if Path(row['file']).is_relative_to(ROOT / 'ikea')], indent=2) + "\n")
    temporary.replace(ikea_database)
    # vscode-clangd's optional reload watcher observes the checkout root, not
    # this custom build directory. Publish only changed bytes to avoid restarts
    # on no-op refreshes; the real compiler database remains in build/clang/dev.
    mirror = ROOT / 'compile_commands.json'
    if storage.managed_root() is not None:
        # A small structural link keeps the extension's root watcher while the
        # generated database body and its replacements stay on bounded storage.
        stamp = BUILD / '.root-database-sha256.json'
        digest = hashlib.sha256(database.read_bytes()).hexdigest()
        previous = json.loads(stamp.read_text()) if stamp.exists() else None
        if (not mirror.is_symlink() or mirror.resolve() != database.resolve() or
                previous != digest):
            temporary = mirror.with_suffix('.tmp')
            temporary.unlink(missing_ok=True)
            temporary.symlink_to(database)
            temporary.replace(mirror)
            storage.write_json(stamp, digest)
    else:
        content = database.read_bytes()
        if not mirror.exists() or mirror.read_bytes() != content:
            temporary = mirror.with_suffix('.tmp')
            temporary.write_bytes(content)
            temporary.replace(mirror)
    print("Active spikes: " + (", ".join(sorted(selected)) or "(none)"))
    print("Active benchmarks: " + (", ".join(sorted(benchmarks)) or "(none)"))
    print(f"Editor compilation database: {database}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--add", action="append", default=[], help="Activate a spike; repeat as needed")
    parser.add_argument("--remove", action="append", default=[], help="Deactivate a spike; repeat as needed")
    parser.add_argument("--add-benchmark", action="append", default=[], help="Activate a recurring benchmark suite")
    parser.add_argument("--remove-benchmark", action="append", default=[], help="Deactivate a benchmark suite")
    parser.add_argument("--list", action="store_true", help="List current selection without configuring")
    parser.add_argument("--watch", action="store_true", help="Watch CMake edits and activate newly added spikes")
    parser.add_argument("--sync", action="store_true", help="Apply one automatic editor refresh now")
    parser.add_argument("--install-watch", action="store_true", help="Install and start the Linux user editor watcher")
    args = parser.parse_args()
    changes = [("spike", args.add, args.remove),
               ("benchmark", args.add_benchmark, args.remove_benchmark)]
    modes = [args.list, args.watch, args.sync, args.install_watch]
    if sum(modes) > 1 or (any(modes) and any(add or remove for _, add, remove in changes)):
        parser.error("--list, --watch, --sync and --install-watch are separate modes")
    for kind, add, remove in changes:
        if set(add) & set(remove):
            parser.error(f"a {kind} cannot be added and removed together")
        for name in add + remove:
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name):
                parser.error(f"invalid {kind} name: {name}")
    storage.initialize(ROOT)
    if args.watch or args.sync or args.install_watch:
        import dev_watch
        if args.install_watch:
            return dev_watch.install()
        if args.watch:
            return dev_watch.watch()
        return dev_watch.sync()
    BUILD.parent.mkdir(parents=True, exist_ok=True)
    with (BUILD.parent / ".dev-configure.lock").open("a") as lock:
        # Concurrent workbench runs merge their selections under one lock.
        fcntl.flock(lock, fcntl.LOCK_EX)
        selected = active_selection("SIXDB_SPIKES")
        benchmarks = active_selection("SIXDB_BENCHMARKS")
        if args.list:
            print("\n".join([*sorted(selected), *("benchmark:" + b for b in sorted(benchmarks))]))
            return 0
        for directory, names, add, remove in [
                ("spikes", selected, args.add, args.remove),
                ("benchmarks", benchmarks, args.add_benchmark, args.remove_benchmark)]:
            names.difference_update(remove)
            names.update(add)
            for name in names:
                if not (ROOT / "workbench" / directory / name / "CMakeLists.txt").is_file():
                    parser.error(f"{directory}/{name} has no CMakeLists.txt; remove it from the active selection")
        return refresh(selected, benchmarks)



if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error))
