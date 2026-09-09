#!/usr/bin/env python3
"""Compile opaque boundaries separately; record layouts and disassembly, not timings."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "workbench/tools"))
from experiment import Run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
output = args.output or Path(os.environ.get("SIXDB_RESULTS", ROOT / "build/experiments/ikea-blocks-abi")) / stamp
run = Run(ROOT, output.resolve(), {"question": "opaque return and CPS carrier ABI", "compiler": "21.1.8"},
          workspace=ROOT / "build/workspaces/ikea-blocks-abi")
source = run.source_root / "workbench/spikes/ikea-blocks/abi"
variants = {
    "aarch64": [],
    "x86-sse2": ["--target=x86_64-linux-gnu", "-march=x86-64"],
    "x86-avx2": ["--target=x86_64-linux-gnu", "-march=x86-64-v3"],
    "x86-avx512": ["--target=x86_64-linux-gnu", "-march=x86-64-v4"],
}
try:
    run.step("compiler", ["clang++-21", "--version"], "compiler.txt")
    run.step("host", ["uname", "-a"], "host.txt")
    run.step("cpu", ["lscpu"], "cpu.txt")
    for name, flags in variants.items():
        build = run.build_dir / name
        command = ["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build),
                   "-DCMAKE_CXX_COMPILER=clang++-21", "-DCMAKE_BUILD_TYPE=",
                   "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_CXX_FLAGS=" + " ".join(flags)]
        if name.startswith("x86"):
            command += ["-DCMAKE_SYSTEM_NAME=Linux", "-DCMAKE_SYSTEM_PROCESSOR=x86_64"]
        run.step(name + "-configure", command)
        run.step(name + "-build", ["cmake", "--build", str(build), "--parallel", "1"])
        (run.output / name).mkdir()
        shutil.copy2(build / "abi_check", run.output / name / "abi_check")
        (run.output / (name + "-commands.json")).write_bytes((build / "compile_commands.json").read_bytes())
        # Objects preserve call relocations and prevent the linker from concealing boundaries.
        for unit in ["producer", "caller", "sink"]:
            obj = build / "CMakeFiles/probe_objects.dir" / (unit + ".cpp.o")
            shutil.copy2(obj, run.output / name / (unit + ".o"))
            run.step(name + "-" + unit, ["llvm-objdump-21", "-dr", "--no-show-raw-insn", str(obj)],
                     name + "-" + unit + ".asm")
        if name != "x86-avx512":
            prefix = [] if name == "aarch64" else ["qemu-x86_64", "-cpu", "max", "-L", "/usr/x86_64-linux-gnu"]
            run.step(name + "-check", prefix + [str(build / "abi_check")], name + "-layout.csv")
        # LLVM IR records target layout, sret/byval lowering and expected's chosen layout.
        run.step(name + "-ir", ["clang++-21", "-std=c++23", "-O3", "-Wno-return-type-c-linkage",
                 *flags, "-S", "-emit-llvm", str(source / "producer.cpp"), "-o", str(run.output / (name + ".ll"))])
    run.step("aarch64-regcall-rejected", ["clang++-21", "-Werror=ignored-attributes", "-c",
             str(source / "regcall-unsupported.cpp"), "-o", str(run.build_dir / "unsupported.o")], check=False)
    record = run.receipt["commands"][-1]
    if record["returncode"] == 0:
        raise RuntimeError("AArch64 unexpectedly accepted regcall; inspect before interpreting")

    # Keep a few complete boundaries; full disassembly/IR stays in the bundle.
    shutil.copyfile(run.output / "aarch64-regcall-rejected.stderr",
                    run.output / "aarch64-regcall-rejected.txt")
    run.step("select-assembly", [sys.executable, str(source.parent / "assembly.py"),
                               "abi", str(run.output), str(run.output)])
    compact = ["compiler.txt", "host.txt", "aarch64-layout.csv", "x86-sse2-layout.csv", "x86-avx2-layout.csv",
               "aarch64-regcall-rejected.txt", *[name + "-excerpts.asm" for name in variants]]
    ir = (run.output / "x86-avx512.ll").read_text()
    signatures = [line for line in ir.splitlines()
                  if line.startswith("%") or (line.startswith("define") and
                     any(name in line for name in ["reg_expected128", "reg_union128", "reg_expected256"]))]
    (run.output / "regcall-lowering.txt").write_text("\n".join(signatures) + "\n")
    compact.append("regcall-lowering.txt")
    run.compact(compact, [])  # These are direct observations; rerun instructions are in README.md.
except Exception as exc:
    run.finish(exc)
    raise
else:
    error = run.finish()
    if error:
        raise error
    print(run.output)
