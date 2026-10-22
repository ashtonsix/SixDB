#!/usr/bin/env python3
"""Run bounded local Intervals qualification with all paired samples retained.

Run on Linux from the repository root; prefix with orb -m ubuntu on macOS.
No CPU model/residency inference is made. Build outside measured windows.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build/clang/intervals"))
    parser.add_argument("--output", type=Path, default=Path("build/intervals/local"))
    parser.add_argument("--cpu", type=int, default=8)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    os.chdir(root)
    build, output = args.build.resolve(), args.output.resolve()
    if output.exists():
        raise SystemExit(f"Choose a fresh output directory: {output}")
    output.mkdir(parents=True)
    binary = build / "workbench/spikes/intervals"
    targets = ["intervals_validate", "intervals_consumer", "intervals_consumer_seams",
               "intervals_consumer_edits", "intervals_directory_before", "intervals_directory_after"]
    with (output / "build.log").open("w") as log:
        subprocess.run(["cmake", "--build", str(build), "--target", *targets, "--parallel", "2"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    sources = {}
    for folder in ["ikea/include/ikea/intervals", "ikea/src/intervals", "ikea/test/intervals",
                   "ikea/include/ikea/boundlist", "ikea/include/ikea/streampack",
                   "ikea/src/streampack", "engine", "workbench/spikes/segment-directory", "workbench/spikes/intervals"]:
        for path in (root / folder).rglob("*"):
            if path.is_file() and "evidence" not in path.parts and path.suffix in {".h", ".cpp", ".py", ".txt"}:
                sources[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    executables = {name: hashlib.sha256((binary / name).read_bytes()).hexdigest()
                   for name in targets if name != "intervals_validate"}
    identity = {"qualification": "exploratory warm local VM; no DRAM or fleet inference",
                "platform": platform.platform(), "machine": platform.machine(), "cpu": args.cpu,
                "compiler": subprocess.check_output(["clang++-21", "--version"], text=True),
                "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                "source_files_sha256": sources, "binary_sha256": executables,
                "command": " ".join(os.sys.argv)}
    (output / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
    commands = []
    for process in range(2):
        cases = [(maximum, count) for maximum in [15, 255, 4095] for count in [63, 256]]
        for maximum, count in cases[::1 if process == 0 else -1]:
            commands.append((f"geometry-p{process}-m{maximum}-n{count}.jsonl", [
                "intervals_consumer", "--maximum", str(maximum), "--count", str(count),
                "--trials", "11", "--repeat", "32"]))
        commands.extend([(f"seams-p{process}.jsonl", ["intervals_consumer_seams"]),
                         (f"edits-p{process}.jsonl", ["intervals_consumer_edits"])])
        for layer, shared in [(4, 0), (3, 4)]:
            for variant in (["before", "after"] if process == 0 else ["after", "before"]):
                commands.append((f"directory-p{process}-l{layer}-{variant}.csv", [
                    f"intervals_directory_{variant}", str(layer), str(shared), "16384", "spread", "32768", "7"]))
    with (output / "commands.json").open("w") as stream:
        json.dump(commands, stream, indent=2)
    for filename, command in commands:
        with (output / filename).open("w") as log:
            subprocess.run(["taskset", "-c", str(args.cpu), str(binary / command[0]), *command[1:]],
                           stdout=log, check=True)
    for name, digest in sources.items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Measured source changed during run: {name}")
    subprocess.run(["python3", str(Path(__file__).with_name("report.py")), str(output)], check=True)
    print(output)


if __name__ == "__main__":
    main()
