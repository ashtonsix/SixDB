#!/usr/bin/env python3
"""Report the compound entry's size and stack accesses from a captured binary.

Stack accesses are printed for inspection, not automatically classified as
payload spills: ABI callee saves and other state may also use the stack.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("--match", default=r"replace_and_sum_delta\(", help="Regex over demangled function names")
args = parser.parse_args()
nm = subprocess.check_output(["llvm-nm-21", "--print-size", "--demangle", str(args.binary)], text=True)
symbols = [line for line in nm.splitlines() if re.search(args.match,line)]
if not symbols:
    raise ValueError("no matching functions")
assembly = subprocess.check_output(
    ["llvm-objdump-21", "--disassemble", "--demangle", "--no-show-raw-insn", str(args.binary)], text=True)
entries = []
for symbol in symbols:
    fields = symbol.split(maxsplit=3)
    if len(fields) != 4 or fields[2].lower() not in {"t","w"}:
        continue
    name = fields[3]
    entry = re.search(r"^[0-9a-f]+ <" + re.escape(name) + r">:\n.*?(?=\n\n|\Z)",
                      assembly, flags=re.MULTILINE | re.DOTALL)
    if not entry:
        raise ValueError("function absent from disassembly: " + name)
    lines = entry[0].splitlines()[1:]
    entries.append({"symbol":symbol, "text_bytes":int(fields[1],16),
        "stack_accesses":[line.strip() for line in lines if re.search(r"\bsp\b|%rsp|%rbp",line)]})
result = {
    "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
    "tools": subprocess.check_output(["llvm-objdump-21", "--version"], text=True).splitlines()[0],
    "interpretation": "Inspect vector payload spills separately from callee saves; this is not timing or a dynamic traffic count.",
}
if len(entries) == 1:
    result.update(entries[0])
else:
    result["entries"] = entries
print(json.dumps(result,indent=2))
