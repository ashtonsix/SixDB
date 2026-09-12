"""Emit assembly and LLVM signatures for the proposed 64-byte carriers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=Path("build/tuple-layout/abi"))
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().with_name("abi.cpp")
profiles = {
    "avx2": ["--target=x86_64-linux-gnu", "-mavx2"],
    "avx512": ["--target=x86_64-linux-gnu", "-mavx512f"],
    "neon": ["--target=aarch64-linux-gnu", "-march=armv8-a+simd"],
}
commands, signatures = [], {}
for name, flags in profiles.items():
    for extension, extra in [("s", []), ("ll", ["-emit-llvm"])]:
        output = args.output / f"{name}.{extension}"
        command = ["clang++-21", "-std=c++23", "-O3", "-Werror", "-S",
                   *extra, *flags, str(source), "-o", str(output)]
        subprocess.run(command, check=True)
        commands.append(command)
    signatures[name] = [line for line in (args.output / f"{name}.ll").read_text().splitlines()
                        if line.startswith("define ")]
receipt = {
    "compiler": subprocess.check_output(["clang++-21", "--version"], text=True),
    "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "commands": commands, "signatures": signatures,
}
(args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
for name, lines in signatures.items():
    print(name + ":")
    print("\n".join(lines))
