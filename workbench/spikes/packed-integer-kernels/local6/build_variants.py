#!/usr/bin/env python3
"""Link one immutable timing object at four placements, then audit the code."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def section(binary, name):
    data = binary.read_bytes()
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    assert header[0][:6] == b"\x7fELF\x02\x01", "ELF64 little-endian required"
    start, stride, count, names = header[6], header[11], header[12], header[13]
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, start + i * stride) for i in range(count)]
    names_header = sections[names]
    names_data = data[names_header[4]:names_header[4] + names_header[5]]
    for item in sections:
        label = names_data[item[0]:].split(b"\0", 1)[0].decode()
        if label == name:
            return item[3], data[item[4]:item[4] + item[5]]
    raise RuntimeError(f"missing section: {name}")


def audit(binary):
    asm = subprocess.check_output(["llvm-objdump-21", "-d", "--section=.seriespack_local6_diag", "--demangle", "--no-show-raw-insn", str(binary)], text=True)
    binary.with_suffix(".asm").write_text(asm)
    symbols = subprocess.check_output(["llvm-nm-21", "-S", "--demangle", str(binary)], text=True)
    sizes = {m[2]: int(m[1], 16) for m in re.finditer(r"^[0-9a-f]+ ([0-9a-f]+) [tTwW] (.+)$", symbols, re.M)}
    public_names = [name for name in sizes if "::decode_complete<" in name
                    and "avx512_ops, 6u, (ikea::seriespack::geometry)0, 0u, unsigned char>" in name]
    assert len(public_names) == 1, "exact captured public Local6 core is missing or ambiguous"
    public_name = public_names[0]
    nm_line = next(line for line in symbols.splitlines() if line.endswith(" " + public_name))
    public_address = int(nm_line.split()[0], 16)
    public_asm = subprocess.check_output(["llvm-objdump-21", "-d", "--demangle", "--no-show-raw-insn",
        f"--start-address={public_address}", f"--stop-address={public_address+sizes[public_name]}", str(binary)], text=True)
    binary.with_suffix(".public.asm").write_text(public_asm)
    asm += "\n" + public_asm
    rodata_address, rodata = section(binary, ".rodata")
    result = {}
    for match in re.finditer(r"^([0-9a-f]+) <([^\n]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)", asm, re.M | re.S):
        address, name = int(match[1], 16), match[2]
        if "::operation<" not in name and name != public_name:
            continue
        size = sizes[name]
        normalized, back_edges, calls, stack = [], [], 0, 0
        rows = [parsed for line in match[3].splitlines()
                if (parsed := re.match(r"\s*([0-9a-f]+):\s+(.+)", line))
                and int(parsed[1], 16) < address + size]
        for index, parsed in enumerate(rows):
            pc, ins = int(parsed[1], 16), parsed[2].split("#", 1)[0].strip()
            next_pc = int(rows[index + 1][1], 16) if index + 1 < len(rows) else address + size
            target = re.search(r"0x([0-9a-f]+) <.+>", ins)
            if target:
                dest = int(target[1], 16)
                if address <= dest < address + size:
                    if dest < pc:
                        back_edges.append({"source_offset": pc-address, "target_offset": dest-address,
                                           "target_mod64": dest % 64})
                    ins = ins[:target.start()] + f"REL+{dest-address}"
            if "(%rip)" in ins:
                displacement = re.search(r"(-?0x[0-9a-f]+)\(%rip\)", ins)
                assert displacement, f"missing RIP displacement in {name}: {parsed[2]}"
                offset = next_pc + int(displacement[1], 16) - rodata_address
                assert 0 <= offset < len(rodata), f"constant outside rodata in {name}"
                ins = re.sub(r"-?0x[0-9a-f]+\(%rip\)", f"CONST+{offset}(%rip)", ins)
            calls += bool(re.match(r"call", ins))
            stack += "(%rsp)" in ins or "(%rbp)" in ins
            normalized.append(ins)
        result[name] = {"address": address, "entry_mod64": address % 64, "size": size,
                        "fixed_public_core": name == public_name,
                        "normalized_sha256": hashlib.sha256("\n".join(normalized).encode()).hexdigest(),
                        "instructions": len(normalized), "calls": calls, "stack_references": stack, "back_edges": back_edges}
    assert result, "no timed operations found"
    return {"binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "rodata_sha256": hashlib.sha256(rodata).hexdigest(), "functions": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("object", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cxx", default="clang++-21")
    parser.add_argument("--link-flag", action="append", default=[])
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"object_sha256": hashlib.sha256(args.object.read_bytes()).hexdigest(),
                "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                "placement_scope": "Raw operation section only; captured public decoder core remains fixed in .text",
                "variants": {}}
    for pad in (0, 16, 32, 48):
        script, binary = args.output / f"pad{pad}.ld", args.output / f"pad{pad}"
        script.write_text("SECTIONS { .seriespack_local6_diag : ALIGN(64) { . += " + str(pad) +
                          "; KEEP(*(.seriespack_local6_diag)); } } INSERT AFTER .text;\n")
        command = [args.cxx, *args.link_flag, str(args.object), str(args.library), f"-Wl,-T,{script}",
                   f"-Wl,-Map,{binary}.map", "-o", str(binary)]
        subprocess.run(command, check=True)
        binary.with_suffix(".link.json").write_text(json.dumps(command, indent=2) + "\n")
        manifest["variants"][str(pad)] = audit(binary)
    reference = manifest["variants"]["0"]
    for pad, variant in manifest["variants"].items():
        assert variant["rodata_sha256"] == reference["rodata_sha256"], "constant bytes changed"
        assert variant["functions"].keys() == reference["functions"].keys(), "function set changed"
        for name, fn in variant["functions"].items():
            before = reference["functions"][name]
            assert fn["size"] == before["size"] and fn["normalized_sha256"] == before["normalized_sha256"], name
            expected_shift = 0 if fn["fixed_public_core"] else int(pad)
            assert (fn["address"] - before["address"]) % 64 == expected_shift, name
    manifest["identical_code_and_constants"] = True
    (args.output / "placement.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"variants": 4, "functions_per_variant": len(reference["functions"]),
                      "identical_code_and_constants": True,
                      "stack_references": sum(v["stack_references"] for v in reference["functions"].values())}))


if __name__ == "__main__":
    main()
