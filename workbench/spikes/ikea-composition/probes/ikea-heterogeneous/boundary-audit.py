#!/usr/bin/env python3
"""Read captured benchmark ELF/disassembly; report boundaries and symbol text.

This does not infer spills or executed traffic from instruction counts. The
live-state interpretation, including indirect stack accesses, is in
notes/boundaries.md. No compiler, benchmark, worker, or source mutation runs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


READER = re.compile(r"ikea_heterogeneous_(direct|local|scan)_(inline|split)$")
FUNCTION = re.compile(
    r"^([0-9a-f]+) <([^\n]+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)", re.M | re.S
)
INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):[^\t]*\t([a-z][a-z0-9.]*)\s*(.*)$")


def elf_function_sizes(data):
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected an ELF64 little-endian captured executable")
    table = struct.unpack_from("<Q", data, 40)[0]
    stride, count = struct.unpack_from("<HH", data, 58)
    sections = [
        struct.unpack_from("<IIQQQQIIQQ", data, table + i * stride)
        for i in range(count)
    ]
    result = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB, retained unstripped capture.
            continue
        for offset in range(section[4], section[4] + section[5], section[9]):
            _, info, _, _, address, size = struct.unpack_from("<IBBHQQ", data, offset)
            if info & 15 == 2:  # STT_FUNC
                result[address] = size
    if not result:
        raise ValueError("Captured ELF lacks its function symbol table")
    return result


def category(name):
    match = READER.fullmatch(name)
    if match:
        return match[2]
    if name in {"ikea_heterogeneous_bec_count1", "ikea_heterogeneous_bec_count2"}:
        return "shared_bec"
    if "count_range<" in name or "CountOperations<" in name:
        if "Execution)0>" in name:
            return "outlined_inline"
        if "Execution)1>" in name:
            return "outlined_split"
    return None


def audit(directory):
    assembly = directory / "assembly.txt"
    executable = directory / "ikea_heterogeneous_bench"
    binary = executable.read_bytes()
    text = assembly.read_text()
    sizes = elf_function_sizes(binary)
    functions = []
    for match in FUNCTION.finditer(text):
        start, name, body = int(match[1], 16), match[2], match[3]
        kind = category(name)
        if kind is None:
            continue
        calls = []
        for line in body.splitlines():
            instruction = INSTRUCTION.match(line)
            if not instruction:
                continue
            address, operation, operand = instruction.groups()
            target = re.search(r"<(.+)>", operand)
            target = target[1] if target else operand
            ordinary_call = operation in {"call", "callq", "bl", "blr"}
            external_tail = operation in {"jmp", "jmpq", "b"} and not (
                target == name or target.startswith(name + "+")
            )
            if ordinary_call or external_tail:
                calls.append({"offset": hex(int(address, 16) - start),
                              "operation": operation, "target": target})
        functions.append({"symbol": name, "category": kind,
                          "address": hex(start), "text_bytes": sizes[start],
                          "calls_or_tail_transfers": calls})
    names = {item["symbol"] for item in functions}
    expected = {f"ikea_heterogeneous_{kind}_{mode}" for kind in
                ("direct", "local", "scan") for mode in ("inline", "split")}
    if not expected <= names:
        raise ValueError("Capture lacks one or more range reader symbols")
    totals = {
        mode: sum(item["text_bytes"] for item in functions
                  if item["category"] in groups)
        for mode, groups in {
            "inline_including_outlined_helpers": {"inline", "outlined_inline"},
            "split_including_shared_bec": {"split", "outlined_split", "shared_bec"},
        }.items()
    }
    run = json.loads((directory / "run.json").read_text())
    return {
        "capture": str(directory), "source_digest": run.get("source_digest"),
        "config": run.get("config"),
        "sha256": {"assembly.txt": hashlib.sha256(assembly.read_bytes()).hexdigest(),
                   executable.name: hashlib.sha256(binary).hexdigest()},
        "all_inline_readers_have_no_calls": all(
            not item["calls_or_tail_transfers"] for item in functions
            if item["category"] == "inline"),
        "text_totals": totals,
        "text_scope": "ELF function sizes; excludes alignment padding, constants, "
                      "unwind tables, checks, and other provider functions",
        "functions": functions,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps([audit(path) for path in args.captures], indent=2))


if __name__ == "__main__":
    main()
