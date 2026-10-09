#!/usr/bin/env python3
"""Local, opt-in correctness/diagnostic capture; run through OrbStack on macOS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
STUDY = Path(__file__).resolve().parent
CHECK = "orbital_representation_conversion_check"
BENCH = "orbital_representation_conversion_bench"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = [ROOT / "CMakeLists.txt"]
    for directory in [STUDY, ROOT / "ikea/include", ROOT / "ikea/src",
                      ROOT / "orbital/include", ROOT / "orbital/src", ROOT / "cmake"]:
        paths += [p for p in directory.rglob("*") if p.is_file() and
                  p.suffix in {".h", ".cpp", ".py", ".cmake", ".txt"}]
    paths += [ROOT / "ikea/CMakeLists.txt", ROOT / "orbital/CMakeLists.txt",
              ROOT / "workbench/spikes/CMakeLists.txt"]
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(set(paths))}


def execute(command, output, label, environment=None):
    print(label, flush=True)
    started = time.monotonic()
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                            timeout=180, env=environment)
    (output / (label + ".stdout")).write_text(result.stdout)
    (output / (label + ".stderr")).write_text(result.stderr)
    receipt = {"command": command, "exit_code": result.returncode,
               "wall_seconds": time.monotonic() - started}
    if result.returncode:
        print(result.stdout[-5000:] + result.stderr[-5000:])
        raise RuntimeError(f"{label} failed ({result.returncode})")
    return receipt, result.stdout


def run(mode, diagnostic, build_root):
    build = build_root / mode
    output = build / "receipt"
    output.mkdir(parents=True, exist_ok=True)
    before = inputs()
    (output / "inputs.json").write_text(json.dumps(before, indent=2) + "\n")
    commands = []
    configure = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DSIXDB_SPIKES=orbital-representation-conversion",
                 "-DSIXDB_MARCH=armv8-a+simd", "-DSIXDB_TUNE=neoverse-v2"]
    if mode == "sanitize":
        configure += ["-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer",
                      "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g",
                      "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"]
    receipt, _ = execute(configure, output, "configure")
    commands.append(receipt)
    targets = [CHECK] + ([BENCH] if diagnostic and mode == "native" else [])
    receipt, _ = execute(["taskset", "-c", "2,3", "cmake", "--build", str(build),
                          "--target", *targets, "-j2"], output, "build")
    commands.append(receipt)
    commands_text = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", CHECK], text=True)
    (output / "target-commands.txt").write_text(commands_text)
    compile_commands = [shlex.split(line) for line in commands_text.splitlines()
                        if " -c " in line and ".cpp" in line]
    if mode == "sanitize":
        for command in compile_commands:
            if "-fsanitize=address,undefined" not in command or "-fsanitize-address-use-after-return=always" not in command:
                raise RuntimeError("Exercised project TU is not fully instrumented")
    env = os.environ.copy()
    if mode == "sanitize":
        env["ASAN_OPTIONS"] = "detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    binary = build / "workbench/spikes/orbital-representation-conversion" / CHECK
    receipt, stdout = execute(["taskset", "-c", "2", str(binary)], output, "check", env)
    commands.append(receipt)
    report = json.loads(stdout)
    bench = None
    if diagnostic and mode == "native":
        bench_binary = binary.with_name(BENCH)
        receipt, stdout = execute(["taskset", "-c", "2", str(bench_binary)], output, "diagnostic")
        commands.append(receipt)
        bench = json.loads(stdout)
    if inputs() != before:
        raise RuntimeError("Qualified source inputs changed during the run")
    receipt = {"mode": mode, "status": "passed", "inputs_sha256": digest(output / "inputs.json"),
               "binary_sha256": digest(binary), "commands": commands,
               "exercised_project_translation_units": len(compile_commands),
               "instrumented": mode == "sanitize", "correctness": report, "diagnostic": bench,
               "limits": ["Local Linux/OrbStack AArch64; no target-hardware or cache-tier claim",
                          "Opaque immutable fixture descriptor, not production Objects/Views",
                          "No isolation, durability or distributed protocol implemented"]}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(str(output / "receipt.json"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["native", "sanitize", "all"], default="native")
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--build-root", type=Path, default=ROOT / "build/ikea-representation-conversion")
    args = parser.parse_args()
    for mode in (["native", "sanitize"] if args.mode == "all" else [args.mode]):
        run(mode, args.diagnostic, args.build_root.resolve())


if __name__ == "__main__":
    main()
