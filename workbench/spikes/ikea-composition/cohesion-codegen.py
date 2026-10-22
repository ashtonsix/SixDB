#!/usr/bin/env python3
"""Compare actual Ikea example objects, including relocations and constants.

Run on Linux with pinned Clang 21.1.8 and the installed GCC cross toolchain.
This is an emitted-code diagnostic, not a runtime or linked-placement comparison.
Only source prefixes and the two documented moved StreamPack type names are
normalized. Instruction bytes, relocation kinds/offsets/addends and data remain.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

SOURCES = (
    "ikea/examples/seriespack/ordinary.cpp",
    "ikea/examples/seriespack/integration.cpp",
    "ikea/examples/filters/composition.cpp",
    "ikea/examples/streampack/ordinary.cpp",
)
# These actual example targets disable compiler vectorization in ikea/CMakeLists.txt.
# Explicit native intrinsics remain enabled in both cohorts.
SOURCE_FLAGS = {
    "ikea/examples/filters/composition.cpp": ["-fno-vectorize", "-fno-slp-vectorize"],
    "ikea/examples/streampack/ordinary.cpp": ["-fno-vectorize", "-fno-slp-vectorize"],
}
PROFILES = {
    "neon-v2": ("aarch64-linux-gnu", "armv8-a+simd", "neoverse-v2", "NEOVERSE_V2"),
    "avx2-zen5": ("x86_64-linux-gnu", "x86-64-v3", "znver5", "ZEN5"),
    "avx512-zen5": ("x86_64-linux-gnu", "znver5", "znver5", "ZEN5"),
}
TUNES = ("GENERIC", "GRANITE_RAPIDS", "ZEN4", "ZEN5", "NEOVERSE_V2", "NEOVERSE_V3")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, capture_output=True, **kwargs).stdout


def normalize(name, source):
    if source == "ikea/examples/streampack/ordinary.cpp":
        name = name.replace("ikea::streampack::detail::compact::geometry",
                            "ikea::streampack::geometry")
        name = name.replace("ikea::intervals::error", "ikea::streampack::error")
    return name


def object_record(path, source, cxxfilt):
    """Read little-endian ELF64 without dropping relocation targets/addends."""
    data = path.read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01", "requires little-endian ELF64"
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, header[6] + i * header[11])
                for i in range(header[12])]

    def body(section):
        return data[section[4]:section[4] + section[5]]

    def string(table, offset):
        return table[offset:table.find(b"\0", offset)].decode()

    names = body(sections[header[13]])
    section_names = [string(names, s[0]) for s in sections]
    symtab = next(s for s in sections if s[1] == 2)
    strings = body(sections[symtab[6]])
    symbols = [struct.unpack_from("<IBBHQQ", data, symtab[4] + i)
               for i in range(0, symtab[5], symtab[9])]
    raw_names = [string(strings, s[0]) for s in symbols]
    # Demangle complete identifiers; do not rewrite raw mangled substrings.
    demangle_inputs = list(dict.fromkeys(raw_names + [
        n.split(".", 2)[-1] for n in section_names
        if n.startswith((".text._Z", ".rodata._Z", ".data._Z", ".gcc_except_table._Z"))]))
    demangled = run([cxxfilt], input="\n".join(demangle_inputs) + "\n").splitlines()
    name_map = dict(zip(demangle_inputs, demangled, strict=True))

    def canonical_name(name):
        return normalize(name_map.get(name, name), source)

    def canonical_section(name):
        for prefix in (".text.", ".rodata.", ".data.", ".gcc_except_table."):
            if name.startswith(prefix) and name[len(prefix):] in name_map:
                return prefix + canonical_name(name[len(prefix):])
        return name

    relocations = {}
    for section in sections:
        if section[1] != 4:
            continue
        assert section[6] == sections.index(symtab)
        rows = []
        for i in range(0, section[5], section[9]):
            offset, info, addend = struct.unpack_from("<QQq", data, section[4] + i)
            symbol = symbols[info >> 32]
            target = ("section:" + canonical_section(section_names[symbol[3]])
                      if symbol[1] & 15 == 3 else canonical_name(raw_names[info >> 32]))
            rows.append([offset, info & 0xffffffff, target, symbol[4], addend])
        relocations[section[7]] = rows
    functions = {}
    for symbol, name in zip(symbols, raw_names):
        _, info, _, section_index, offset, size = symbol
        if info & 15 != 2 or not section_index or not size:
            continue
        key = canonical_name(name)
        assert key not in functions, f"normalized function collision: {key}"
        functions[key] = {
            "size": size,
            "section_alignment": sections[section_index][8],
            "bytes": body(sections[section_index])[offset:offset + size].hex(),
            "relocations": [[r[0] - offset, *r[1:]] for r in relocations.get(section_index, [])
                            if offset <= r[0] < offset + size],
        }
    constants = {}
    unwind = {}
    for i, section in enumerate(sections):
        if section[1] != 1 or not section[2] & 2 or section[2] & 4:
            continue
        key = canonical_section(section_names[i])
        row = {"bytes": body(section).hex(), "alignment": section[8],
               "relocations": relocations.get(i, [])}
        target = unwind if key.startswith((".eh_frame", ".gcc_except_table")) else constants
        assert key not in target, f"normalized section collision: {key}"
        target[key] = row
    return {"sha256": sha(data), "functions": functions, "constants": constants, "unwind": unwind}


def compare(left, right):
    names = sorted(left.keys() | right.keys())
    return [{"name": name, "before_present": name in left, "after_present": name in right,
             "equal": left.get(name) == right.get(name),
             "before": left.get(name), "after": right.get(name)} for name in names]


def command(compiler, tree, source, profile, output):
    target, march, tune, macro = PROFILES[profile]
    return [compiler, f"--target={target}", "--gcc-toolchain=/usr", "-std=c++23", "-O3",
            "-fno-fast-math", "-ffp-contract=off", "-fdenormal-fp-math=ieee",
            "-fvisibility=hidden", "-fvisibility-inlines-hidden", "-ffunction-sections",
            "-fdata-sections", f"-march={march}", f"-mtune={tune}",
            *SOURCE_FLAGS.get(source, []),
            *[f"-DSIXDB_TUNE_{t}={int(t == macro)}" for t in TUNES],
            f"-ffile-prefix-map={tree}=/sixdb", "-I", str(tree / "ikea/include"),
            "-MD", "-MF", str(output.with_suffix(".d")), "-c", str(tree / source),
            "-o", str(output)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--compiler", default="clang++-21")
    parser.add_argument("--objdump", default="llvm-objdump-21")
    parser.add_argument("--cxxfilt", default="llvm-cxxfilt-21")
    parser.add_argument("--baseline-commit", required=True)
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--reuse", action="store_true",
                        help="reuse existing objects only when commands, objects and every dependency match")
    args = parser.parse_args()
    trees = {"before": args.before.resolve(), "after": args.after.resolve()}
    output = args.output.resolve()
    commands = []
    for profile in PROFILES:
        for source in SOURCES:
            for arm, tree in trees.items():
                obj = output / profile / arm / (source.removeprefix("ikea/examples/").replace("/", "-") + ".o")
                commands.append((profile, source, arm, obj,
                                 command(args.compiler, tree, source, profile, obj)))
    if args.plan:
        for _, _, _, _, cmd in commands:
            print(shlex.join(cmd))
        return
    output.mkdir(parents=True, exist_ok=True)
    version = run([args.compiler, "--version"])
    assert "clang version 21.1.8" in version, version
    receipt = {"scope": __doc__, "baseline_commit": args.baseline_commit,
               "compiler_version": version, "objdump_version": run([args.objdump, "--version"]),
               "source_flags": SOURCE_FLAGS,
               "normalizations": ["both source roots mapped to /sixdb", "StreamPack TU: compact::geometry -> streampack::geometry", "StreamPack TU: intervals::error -> streampack::error"],
               "compiles": [], "pairs": []}
    records = {}
    dependency_hashes = {}
    previous_compiles, previous_objects, previous_hashes = {}, {}, {}
    prior_path = output / "receipt.json"
    if args.reuse and prior_path.exists():
        prior_data = prior_path.read_bytes()
        prior = json.loads(prior_data)
        assert prior["compiler_version"] == version
        assert prior["baseline_commit"] == args.baseline_commit
        previous_compiles = {(r["profile"], r["source"], r["arm"]): r for r in prior["compiles"]}
        previous_objects = {(r["profile"], r["source"], arm): r[f"{arm}_object_sha256"]
                            for r in prior["pairs"] for arm in trees}
        previous_hashes = prior["dependency_sha256"]
        receipt["reused_receipt_sha256"] = sha(prior_data)
        (output / "receipt-prior.json").write_bytes(prior_data)

    def dependency_hash(dependency):
        if dependency not in dependency_hashes:
            dependency_hashes[dependency] = sha(Path(dependency).read_bytes())
        return dependency_hashes[dependency]

    for profile, source, arm, obj, cmd in commands:
        obj.parent.mkdir(parents=True, exist_ok=True)
        key = profile, source, arm
        previous = previous_compiles.get(key)
        reused = bool(previous and previous["command"] == cmd and obj.exists()
                      and previous_objects[key] == sha(obj.read_bytes())
                      and all(dependency_hash(d) == previous_hashes[d] for d in previous["dependencies"]))
        print(f"{'reuse verified' if reused else 'compile'} {profile} {arm} {source}", flush=True)
        if not reused:
            process = subprocess.run(cmd, text=True, capture_output=True)
            obj.with_suffix(".log").write_text(process.stdout + process.stderr)
            process.check_returncode()
        dep_text = obj.with_suffix(".d").read_text().replace("\\\n", " ")
        dependencies = shlex.split(dep_text.split(":", 1)[1])
        for dependency in dependencies:
            dependency_hash(dependency)
        receipt["compiles"].append({"profile": profile, "source": source, "arm": arm,
                                    "command": cmd, "dependencies": dependencies, "reused": reused})
        obj.with_suffix(".s").write_text(run([args.objdump, "-d", "-r", "--demangle", str(obj)]))
        obj.with_suffix(".sections").write_text(run([args.objdump, "-s", str(obj)]))
        records[profile, source, arm] = object_record(obj, source, args.cxxfilt)
    for profile in PROFILES:
        for source in SOURCES:
            before, after = (records[profile, source, arm] for arm in trees)
            pair = {"profile": profile, "source": source,
                    "before_object_sha256": before["sha256"],
                    "after_object_sha256": after["sha256"]}
            for kind in ("functions", "constants", "unwind"):
                rows = compare(before[kind], after[kind])
                pair[kind] = {"total": len(rows), "equal": sum(r["equal"] for r in rows),
                              "before_sha256": sha(json.dumps(before[kind], sort_keys=True).encode()),
                              "after_sha256": sha(json.dumps(after[kind], sort_keys=True).encode()),
                              "differences": [r for r in rows if not r["equal"]]}
            receipt["pairs"].append(pair)
            print(f"{profile} {source}: " + ", ".join(
                f"{k} {pair[k]['equal']}/{pair[k]['total']} equal" for k in ("functions", "constants", "unwind")), flush=True)
    receipt["dependency_sha256"] = dependency_hashes
    receipt_data = (json.dumps(receipt, indent=2) + "\n").encode()
    (output / "receipt.json").write_bytes(receipt_data)
    summary = {key: value for key, value in receipt.items()
               if key not in ("compiles", "dependency_sha256", "reused_receipt_sha256")}
    summary["receipt_sha256"] = sha(receipt_data)
    summary["dependency_paths"] = len(dependency_hashes)
    summary["dependency_manifest_sha256"] = sha(json.dumps(dependency_hashes, sort_keys=True).encode())
    summary["command_sha256"] = [sha(json.dumps(c["command"]).encode()) for c in receipt["compiles"]]
    summary["limits"] = "ELF objects, not linked placement or timings. External compiled callees are targets, not recompiled callee bodies. Assertion/exception data bytes and all branch immediates remain literal."
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
