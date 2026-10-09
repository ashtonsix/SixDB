#!/usr/bin/env python3
"""Capture this single access family on V2 and Zen5; compile-only, no timing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

STACK_NOTE = ("Stack-related sites include frame setup. Stack-memory sites exclude address-only "
              "LEA/NOP and include explicit push/pop, but not implicit call/return stack effects. "
              "They include spills, ABI save/restore and outgoing arguments; inspect the "
              "retained disassembly for meaning.")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(argv):
    return subprocess.check_output(argv, text=True)


def dependencies(path, root):
    # Clang -MD includes system headers. Make continuation/escaping syntax is
    # sufficient here; the source tree and pinned sysroot contain no newlines.
    text = path.read_text().replace("\\\n", "")
    names = shlex.split(text.split(":", 1)[1])
    return sorted({(root / name).resolve() for name in names})


def relevant(name):
    return (name.startswith("access_") or name.startswith("access_spike::Family<") or
            name.startswith("loom::detail::MemoryRing<"))


def role(name):
    if name == "access_ring_spin":
        return "ordinary_spin_boundary"
    if name.startswith("access_") and "::" not in name:
        return "ordinary_kernel"
    if "::work(" in name:
        return "native_callback"
    if "::enter(" in name:
        return "ordinary_aapcs_entry_bridge"
    if "::yield(" in name:
        return "recurrent_yield"
    if "::retire_yield(" in name:
        return "completion_yield"
    if "::spin(" in name:
        return "ordinary_spin_body"
    return "admission_or_ring_support"


def inspect(obj, arch, destination=None):
    destination = destination or obj
    assembly = run(["llvm-objdump-21", "-dr", "--demangle", "--no-show-raw-insn", str(obj)])
    destination.with_suffix(".asm").write_text(assembly)
    symbols = run(["llvm-nm-21", "-S", "--demangle", "--radix=d", str(obj)])
    destination.with_suffix(".nm").write_text(symbols)
    sizes = {}
    for line in symbols.splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) == 4 and fields[0].isdigit() and fields[1].isdigit():
            sizes[fields[3]] = int(fields[1])
    bodies, current = {}, None
    for line in assembly.splitlines():
        match = re.match(r"^[0-9a-f]+ <(.*)>:", line)
        if match:
            current = match[1]
            bodies[current] = []
        elif current:
            if "R_AARCH64_" in line or "R_X86_64_" in line:
                if bodies[current]:
                    bodies[current][-1]["relocations"].append(line.strip())
                continue
            match = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
            if match:
                bodies[current].append({"offset": match[1], "op": match[2],
                                        "args": match[3], "relocations": []})

    functions = []
    for name, body in bodies.items():
        if not relevant(name):
            continue
        if arch == "arm":
            frame_pointer = any(re.match(r"(?:mov x29, sp|add x29, sp,)",
                                          site["op"] + " " + site["args"]) for site in body)
            def is_stack(site):
                return bool(re.search(r"\bsp\b", site["args"]) or
                            (frame_pointer and re.search(r"\bx29\b", site["args"])))
            def is_stack_memory(site):
                return bool(re.search(r"\[(?:sp|x29)\b", site["args"]) and
                            ("[sp" in site["args"] or frame_pointer))
            def is_vector(site):
                return bool(re.search(r"\b(?:[qds]\d+|v\d+(?:\.[0-9]+[bhsd])?)\b", site["args"]))
            calls = [site for site in body if site["op"] in ("bl", "blr")]
            conditional = [site for site in body if site["op"].startswith("b.") or
                           site["op"] in ("cbz", "cbnz", "tbz", "tbnz")]
            indirect = [site for site in body if site["op"] in ("br", "blr")]
            tails = [site for site in body if site["op"] in ("b", "br")]
        else:
            frame_pointer = any(re.match(r"movq? %rsp, %rbp", site["op"] + " " + site["args"])
                                for site in body)
            def is_stack(site):
                return bool(re.search(r"%rsp\b", site["args"]) or
                            (frame_pointer and re.search(r"%rbp\b", site["args"])) or
                            re.match(r"^(?:push|pop)(?:q|l|w)?$", site["op"]))
            def is_stack_memory(site):
                if site["op"].startswith(("lea", "nop")):
                    return False
                return bool(re.search(r"\([^)]*%rsp\b", site["args"]) or
                            (frame_pointer and re.search(r"\([^)]*%rbp\b", site["args"])) or
                            re.match(r"^(?:push|pop)(?:q|l|w)?$", site["op"]))
            def is_vector(site):
                return bool(re.search(r"%(?:xmm|ymm|zmm)\d+\b", site["args"]))
            calls = [site for site in body if site["op"] in ("call", "callq")]
            conditional = [site for site in body if site["op"].startswith("j") and
                           site["op"] not in ("jmp", "jmpq")]
            indirect = [site for site in body if site["op"] in ("call", "callq", "jmp", "jmpq") and
                        site["args"].startswith("*")]
            tails = [site for site in body if site["op"] in ("jmp", "jmpq")]
        stack = [site for site in body if is_stack(site)]
        memory = [site for site in body if is_stack_memory(site)]
        vector = [site for site in memory if is_vector(site)]
        hints = [site for site in body if site["op"] == "prfm" or site["op"].startswith("prefetch")]
        functions.append({"name": name, "role": role(name), "symbol_bytes": sizes.get(name),
                          "static_instruction_sites": len(body),
                          "ordinary_call_sites": calls,
                          "stack_related_sites": stack,
                          "stack_memory_sites": memory,
                          "vector_stack_memory_sites": vector,
                          "conditional_branch_sites": conditional,
                          "indirect_control_transfer_sites": indirect,
                          "tail_or_jump_sites": tails,
                          "prefetch_instruction_sites": hints})
    size_output = run(["llvm-size-21", "--format=sysv", str(obj)])
    destination.with_suffix(".size").write_text(size_output)
    text_sections = {}
    for line in size_output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0].startswith(".text") and fields[1].isdigit():
            text_sections[fields[0]] = int(fields[1])
    return {"object_bytes": obj.stat().st_size, "text_bytes": sum(text_sections.values()),
            "text_sections": text_sections, "functions": functions}


def short_name(name):
    if name.startswith("access_") and "::" not in name:
        return name
    for member in ("work", "yield", "retire_yield", "enter", "spin", "enqueue", "push_selected", "promote"):
        if "::" + member + "(" in name:
            return member
    return name


def summarize(target, obj, functions):
    return [{"target": target, "object": obj.name,
             "name": short_name(function["name"]), "role": function["role"],
             "bytes": function["symbol_bytes"],
             "static_sites": function["static_instruction_sites"],
             "calls": len(function["ordinary_call_sites"]),
             "stack_memory": len(function["stack_memory_sites"]),
             "vector_stack_memory": len(function["vector_stack_memory_sites"]),
             "prefetch_sites": len(function["prefetch_instruction_sites"])} for function in functions]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse", type=Path, help="Reinspect hash-verified compiled objects without recompiling")
    args = parser.parse_args()
    root, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / "codegen-recipe.py").write_bytes(Path(__file__).read_bytes())
    if args.reuse:
        previous = args.reuse.resolve() / "receipt.json"
        receipt = json.loads(previous.read_text())
        receipt["reinspected_receipt_sha256"] = sha(previous)
        receipt["analyzer_sha256"] = sha(Path(__file__).resolve())
        receipt["stack_site_qualification"] = STACK_NOTE
        summaries = []
        for target, capture in receipt["targets"].items():
            directory = output / target
            directory.mkdir()
            for path, digest in capture["dependency_sha256"].items():
                if sha(Path(path)) != digest:
                    raise RuntimeError("Compiled dependency changed: " + path)
            for entry in capture["objects"]:
                obj = Path(entry["argv"][-1])
                if sha(obj) != entry["object_sha256"]:
                    raise RuntimeError("Compiled object changed: " + str(obj))
                entry.update(inspect(obj, "arm" if target == "v2" else "x86", directory / obj.name))
                summaries += summarize(target, obj, entry["functions"])
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        (output / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")
        print("Reinspected hash-verified V2/Zen5 objects and dependencies; no recompilation")
        print(json.dumps(summaries, indent=2))
        return
    study = root / "workbench/spikes/call-boundaries/access-interleaving"
    # Instantiates the actual public spin body in a separate ordinary TU.
    # kernels.cpp itself is compiled directly, with no source amalgamation.
    bridge = output / "codegen-entry.cpp"
    bridge.write_text('#include "fixture.h"\nextern "C" [[gnu::noinline]] std::uint64_t '
                      'access_ring_spin(access_spike::Native::Ring* ring, std::uint64_t visits) {\n'
                      '  return ring->spin({visits});\n}\n')
    compiler = run(["clang++-21", "--version"])
    if not re.search(r"\b21\.1\.8\b", compiler):
        raise ValueError("This recipe requires pinned Clang 21.1.8")
    common = ["-std=c++23", "-O3", "-g0", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
              "-ffp-contract=off", "-fno-fast-math", "-fdenormal-fp-math=ieee",
              "-fvisibility=hidden", "-fvisibility-inlines-hidden", "-ffunction-sections", "-fdata-sections",
              "-I" + str(study), "-I" + str(root / "loom/include"),
              "-I" + str(root / "orbital/include"), "-I" + str(root / "ikea/include")]
    cross = ["--target=x86_64-linux-gnu", "--sysroot=/usr/x86_64-linux-gnu",
             "-isystem", "/usr/x86_64-linux-gnu/include/c++/15",
             "-isystem", "/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu"]
    targets = {"v2": ("arm", ["-mcpu=neoverse-v2", "-mtune=neoverse-v2", "-DSIXDB_TUNE_NEOVERSE_V2=1"]),
               "zen5": ("x86", cross + ["-march=znver5", "-mtune=znver5", "-DSIXDB_TUNE_ZEN5=1"])}
    receipt = {"compiler": compiler, "source_root": str(root),
               "recipe_sha256": sha(Path(__file__).resolve()), "targets": {},
               "stack_site_qualification": STACK_NOTE,
               "qualification": "Static relocatable objects; V2 and Zen5 compile-only.",
               "limits": ["Instruction, branch, hint, call and stack counts are static sites in the union of paths; not executed counts or cycles.",
                          STACK_NOTE,
                          "The ordinary spin boundary and AArch64 AAPCS entry bridge are separate from recurrent native callback/yield paths.",
                          "Clang may inline yield/resume into callback or spin. Absence of an outlined yield symbol does not mean absence of its instructions.",
                          "Object text bytes include emitted support and cold/assertion paths. Function sections can be discarded or merged when linking.",
                          "One actual native_reader<64> family. No ABI-capacity matrix, llvm-mca analysis, or hardware-performance claim.",
                          "-MD dependency manifests include transitive project and installed system headers; native and cross sysroots are separately recorded."]}
    summaries, captured_deps = [], {}
    for target, (arch, flags) in targets.items():
        directory = output / target
        directory.mkdir()
        entries, all_deps = [], {}
        for source in (study / "kernels.cpp", bridge):
            obj = directory / (source.stem + ".o")
            depfile = obj.with_suffix(".d")
            command = ["clang++-21", *common, *flags, "-MD", "-MF", str(depfile),
                       "-c", str(source), "-o", str(obj)]
            subprocess.run(command, check=True)
            deps = {str(path): sha(path) for path in dependencies(depfile, root)}
            for path, digest in deps.items():
                if path in all_deps and all_deps[path] != digest:
                    raise RuntimeError("Dependency changed during capture: " + path)
                if path in captured_deps and captured_deps[path] != digest:
                    raise RuntimeError("Dependency changed between targets: " + path)
                all_deps[path] = digest
                captured_deps[path] = digest
            report = inspect(obj, arch)
            entries.append({"source": str(source), "argv": command,
                            "object_sha256": sha(obj), "depfile_sha256": sha(depfile),
                            "dependency_sha256": deps, **report})
            summaries += summarize(target, obj, report["functions"])
        for path, digest in all_deps.items():
            if sha(Path(path)) != digest:
                raise RuntimeError("Dependency changed during capture: " + path)
        names = {f["name"] for entry in entries for f in entry["functions"]}
        if not {"access_scalar", "access_manual_1", "access_manual_2", "access_manual_4",
                "access_manual_8", "access_ring_spin"}.issubset(names):
            raise RuntimeError("Missing expected exported kernels")
        roles = {f["role"] for entry in entries for f in entry["functions"]}
        if not {"native_callback", "completion_yield"}.issubset(roles):
            raise RuntimeError("Missing native callback/completion family")
        if arch == "arm" and "ordinary_aapcs_entry_bridge" not in roles:
            raise RuntimeError("Missing AArch64 AAPCS entry bridge")
        receipt["targets"][target] = {"objects": entries,
                                      "outlined_yield_present": "recurrent_yield" in roles,
                                      "dependency_sha256": all_deps}
        print(f"{target}: compiled actual kernels and separate Ring entry, {len(all_deps)} dependencies", flush=True)
    for path, digest in captured_deps.items():
        if sha(Path(path)) != digest:
            raise RuntimeError("Dependency changed before receipt: " + path)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    (output / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")
    print(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()
