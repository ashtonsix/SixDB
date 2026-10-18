#!/usr/bin/env python3
"""Audit compiled mask witnesses. Counts diagnose codegen, not CPU timings."""
import argparse
import collections
import hashlib
import json
import pathlib
import re

parser = argparse.ArgumentParser()
parser.add_argument("disassembly", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
functions = {}
name = None
for line in args.disassembly.read_text().splitlines():
    match = re.match(r"^[0-9a-f]+ <((?:mask|tuple|control)_\d+_.*)>:", line)
    if match:
        name = match[1]
        functions[name] = []
    elif line.startswith("Disassembly of section"):
        name = None
    elif name:
        match = re.match(r"\s*[0-9a-f]+:\s+(?:[0-9a-f]{2,8}\s+)+\s*([a-z][a-z0-9.]*)\s*(.*)", line)
        if match:
            functions[name].append((match[1], match[2]))

assert sum(name.startswith("mask_") for name in functions) in (20, 40, 60)
assert sum(name.startswith("tuple_") for name in functions) == 10
assert sum(name.startswith("control_") for name in functions) in (0, 20)
report = {"sha256": hashlib.sha256(args.disassembly.read_bytes()).hexdigest(),
          "functions": {}, "failures": []}
for name, instructions in functions.items():
    counts = dict(collections.Counter(op for op, _ in instructions))
    calls = [(op, operands) for op, operands in instructions if op in ("bl", "blr", "call", "callq")]
    # The intended NEON u64 lane extracts and TuplePack8 ABI move are allowed.
    # Halfword extracts and byte/halfword inserts flag the original defect.
    scalarized = [(op, operands) for op, operands in instructions
                  if (op == "umov" and re.search(r"\.h\[", operands))
                  or (op == "mov" and re.match(r"v\d+\.[bh]\[", operands))
                  or re.match(r"v?(?:pextr|pinsr)[bwd]", op)]
    stack = [(op, operands) for op, operands in instructions
             if re.search(r"\[(?:sp|x29)(?:,|\])|\(%rsp\)", operands)]
    report["functions"][name] = {"instructions": len(instructions), "opcodes": counts,
                                  "calls": calls, "scalarized": scalarized, "stack": stack}
    # TuplePack64 can enter through aggregate stack arguments on x86. Raw
    # vector witnesses enter in SIMD registers and should never spill.
    if calls or scalarized or (name.startswith("mask_") and stack):
        report["failures"].append(name)
args.output.write_text(json.dumps(report, indent=2) + "\n")
assert not report["failures"], report["failures"]
print(f"{len(functions)} mask and TuplePack witnesses: no scalarized narrowing, calls or raw-vector spills")
