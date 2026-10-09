#!/usr/bin/env python3
"""Repeat the frozen Orbital census; output is ignored, no production mutation."""
import argparse
import io
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="7f73c81")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if platform.system() != "Linux":
        parser.error("run with orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/orbital/run.py")
    probe = Path(__file__).resolve().parent
    root = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], cwd=probe, text=True).strip())
    revision = subprocess.check_output(["git", "rev-parse", args.revision], cwd=root, text=True).strip()
    output = root / "build/orbital-mvcc-metadata"
    source = output / "source"
    source.mkdir(parents=True, exist_ok=True)
    marker = source / ".revision"
    if not marker.exists() or marker.read_text().strip() != revision:
        archive = subprocess.check_output(["git", "archive", revision, "orbital", "loom"], cwd=root)
        shutil.rmtree(source)
        source.mkdir()
        with tarfile.open(fileobj=io.BytesIO(archive)) as files:
            files.extractall(source, filter="data")
        marker.write_text(revision + "\n")
    version = subprocess.check_output(["clang++-21", "--version"], text=True).splitlines()[0]
    if "21.1.8" not in version:
        parser.error(f"expected pinned Clang 21.1.8, found {version}")
    sys.path.insert(0, str(root / "workbench/tools"))
    from experiment import Run
    out = (args.output or output / "runs" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")).resolve()
    run = Run(root, out, {"revision": revision, "compiler": version, "machine": platform.machine(),
                         "claim": "requested metadata and ordinary emitted return ABI; no timing claim"},
              workspace=output / "workspace")
    probe = run.source_root / probe.relative_to(root)
    build = run.build_dir
    build.mkdir(exist_ok=True)
    flags = ["clang++-21", "-std=c++23", "-O3", "-fno-fast-math", "-ffp-contract=off",
             "-pthread", "-I" + str(source), "-I" + str(source / "orbital/include")]
    error = None
    try:
        run.step("build-census", flags + [probe / "census.cpp", "-o", build / "census"])
        run.step("build-compact-census", flags + [probe / "compact_census.cpp", "-o", build / "compact-census"])
        abi_flags = flags + ["-Wno-return-type-c-linkage", "-S", "-emit-llvm"]
        for name, filename in [("baseline", "abi.cpp"), ("compact", "compact_abi.cpp"),
                               ("packet", "packet_candidates.cpp")]:
            run.step(f"abi-{name}", abi_flags + [probe / filename, "-o", out / f"{name}-abi.ll"])
        objects = []
        for path in sorted((source / "orbital/src").rglob("*.cpp")):
            target = build / f"{path.stem}-{path.parent.name}.o"
            run.step(f"build-{target.stem}", flags + ["-c", path, "-o", target])
            objects.append(target)
        run.step("build-backing", flags + [probe / "backing.cpp", *objects, "-o", build / "backing"])
        run.step("build-compact-backing", flags + [probe / "compact_backing.cpp",
                 probe / "descriptor/descriptor.cpp", *objects, "-o", build / "compact-backing"])
        run.step("build-escape-backing", flags + [probe / "escape_backing.cpp",
                 probe / "descriptor/descriptor.cpp", *objects, "-o", build / "escape-backing"])
        run.step("build-packet", flags + ["-Wno-return-type-c-linkage",
                 probe / "packet_candidates.cpp", "-o", build / "packet-candidates"])
        for executable, name in [("census", "sizes.jsonl"), ("compact-census", "compact-sizes.jsonl"),
                                 ("backing", "backing.jsonl"),
                                 ("packet-candidates", "packet-candidates.txt"),
                                 ("compact-backing", "compact-backing.jsonl"),
                                 ("escape-backing", "escape-backing.jsonl")]:
            run.step(executable, [build / executable], name)
    except Exception as exc:
        error = exc
    error = run.finish(error)
    if error:
        raise error
    print(f"Census complete: {out}")


if __name__ == "__main__":
    main()
