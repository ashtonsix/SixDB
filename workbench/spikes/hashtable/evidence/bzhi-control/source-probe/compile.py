#!/usr/bin/env python3
"""Compile-only, header-free lowering probe using the pinned Linux compiler."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
COMMON = ["clang++-21", "-std=c++23", "-O3", "-fno-vectorize",
          "-fno-slp-vectorize", "-fno-unroll-loops", "-ffp-contract=off",
          "-fno-fast-math", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
TARGETS = {
    "zen5": ["--target=x86_64-linux-gnu", "-march=x86-64-v3", "-mtune=znver5"],
    "granite": ["--target=x86_64-linux-gnu", "-march=x86-64-v4", "-mtune=graniterapids"],
    "x86_no_bmi2": ["--target=x86_64-linux-gnu", "-march=x86-64"],
    "neon_fallback": ["--target=aarch64-linux-gnu", "-march=armv8-a+simd", "-mtune=neoverse-v2"],
}
WIDTHS = [0, 1, 8, 16, 24, 32, 33, 40, 48, 56, 63, 64, 65, 256, "4294967295u"]


def body(assembly, name):
    match = re.search(r"^" + re.escape(name) + r":.*?^\s*\.size\s+" +
                      re.escape(name) + r"[^\n]*", assembly, re.M | re.S)
    assert match, name
    return match.group(0)


def instructions(function):
    return [line.split("#")[0].split("//")[0].strip()
            for line in function.splitlines()
            if line.startswith("\t") and not line.lstrip().startswith((".", "#", "//"))]


def count(function, opcode):
    return sum(bool(re.match(opcode + r"\b", ins)) for ins in instructions(function))


def facts(assembly, bmi2):
    result = {}
    for width in WIDTHS:
        cpp = body(assembly, f"cpp_{width}")
        for spelling in ["split", "pair"]:
            f = body(assembly, f"{spelling}_{width}")
            w = int(str(width).rstrip("u"))
            if bmi2 and 33 <= w <= 63:
                assert count(f, "bzhiq") == 1, (spelling, width)
                assert count(f, "movl") == 1, (spelling, width)
                assert not count(f, "movb"), (spelling, width)
            else:
                assert instructions(f) == instructions(cpp), (spelling, width)
    for name in ["constant_value", "bounded_result"]:
        assert not count(body(assembly, name), "bzhiq"), name
    assert not count(body(assembly, "bounded_result"), "movl"), "zero-result should fold"
    for name in ["cpp", "split", "pair", "hint"]:
        producer = body(assembly, f"{name}_producer")
        loop = body(assembly, f"{name}_loop")
        assert not count(loop, "callq?"), name
        result[name] = {
            "producer_bzhi": count(producer, "bzhiq"),
            "loop_bzhi": count(loop, "bzhiq"),
            "loop_bextr": count(loop, "bextrq"),
            "loop_push": count(loop, "pushq"),
        }
    if bmi2:
        assert result["cpp"]["producer_bzhi"] == 1
        assert result["split"]["producer_bzhi"] == 2
        assert result["pair"]["producer_bzhi"] == 2
    return result


def main():
    report = {
        "qualification": "Compile-only source experiment; no runtime or performance result.",
        "compiler": subprocess.check_output(["clang++-21", "--version"], text=True).splitlines()[0],
        "sources": {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                    for name in ["mask_probe.h", "probe.cpp", "compile.py"]},
        "targets": {},
    }
    assert "21.1.8" in report["compiler"], report["compiler"]
    for target, flags in TARGETS.items():
        command = COMMON + flags + ["-S", "probe.cpp", "-o", "-"]
        assembly = subprocess.check_output(command, cwd=HERE, text=True)
        object_command = COMMON + flags + ["-c", "probe.cpp", "-o", "/dev/null"]
        subprocess.run(object_command, cwd=HERE, check=True)
        report["targets"][target] = {
            "command": command,
            "assembled": object_command,
            "assembly_sha256": hashlib.sha256(assembly.encode()).hexdigest(),
            "checked": facts(assembly, target in ["zen5", "granite"]),
        }
        if target == "zen5":
            (HERE / "zen5-codegen.txt").write_text(assembly)
    (HERE / "codegen.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: four target compiles, width/constant/fallback/codegen checks; no execution")


if __name__ == "__main__":
    main()
