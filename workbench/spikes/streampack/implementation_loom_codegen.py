#!/usr/bin/env python3
"""Qualify the actual StreamPack Loom consumer TU; static evidence, no workers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def output(argv):
    return subprocess.check_output(argv, text=True)


def dependencies(path, root):
    names = shlex.split(path.read_text().replace("\\\n", " ").split(":", 1)[1])
    return sorted({(root / name).resolve() for name in names})


def role(name):
    if "::Native<" in name and ("::geometry(" in name or "::payload(" in name):
        return "ring_geometry" if "::geometry(" in name else "ring_payload"
    if "native_chain<ikea::streampack::pipeline_location" in name:
        if "::stage<" in name:
            return "point_chain_geometry" if "::chain_geometry" in name else "point_chain_payload"
        if "::completion<" in name:
            return "terminal_chain_completion"
        if "::enter(" in name:
            return "ordinary_chain_entry"
    if "::MemoryRing<" in name:
        if "::yield(" in name:
            return "ring_yield"
        if "::retire_yield(" in name:
            return "ring_retirement"
        if "::enter(" in name:
            return "ordinary_ring_entry"
        return "ring_setup_or_support"
    if any(part in name for part in ("geometry_body<", "payload_body<", "metadata_hints<",
                                    "payload_hints<", "MetadataHints::operator()",
                                    "visit_value_metadata<", "::locate(", "prefix16_native<",
                                    "::chain_geometry(", "::chain_payload(")):
        return "authored_body_or_hint_callable"
    if "::typed<" in name or "::assess(" in name or "::Manual<" in name:
        return "ordinary_driver_or_admission"
    return "other_support"


RECURRING = {"ring_geometry", "ring_payload", "point_chain_geometry", "point_chain_payload"}
HELPER_CALL = re.compile(r"geometry_body|payload_body|metadata_hints|payload_hints|MetadataHints|"
                         r"visit_value_metadata|prefix16_native|::locate\(|::live\(|"
                         r"::chain_geometry\(|::chain_payload\(|loom::prefetch|prefetch_selected")


def inspect(obj, architecture):
    # Same objdump/nm and relocation conventions as the existing Loom ABI audit.
    assembly = output(["llvm-objdump-21", "-dr", "--demangle", "--no-show-raw-insn", str(obj)])
    obj.with_suffix(".asm").write_text(assembly)
    symbols = output(["llvm-nm-21", "-S", "--demangle", "--radix=d", str(obj)])
    obj.with_suffix(".nm").write_text(symbols)
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
                    bodies[current][-1] += " [relocation: " + line.strip() + "]"
                continue
            match = re.match(r"^\s*[0-9a-f]+:\s+(\S+)\s*(.*)", line)
            if match:
                bodies[current].append(match[1] + " " + match[2])
    result = []
    for name, body in bodies.items():
        kind = role(name)
        if kind == "other_support":
            continue
        calls = [line for line in body if re.match(r"^(bl|blr|call|callq) ", line)]
        if architecture == "arm":
            frame = any(re.match(r"(?:mov x29, sp|add x29, sp,)", line) for line in body)
            stack = [line for line in body if re.search(r"\bsp\b", line) or
                     (frame and re.search(r"\bx29\b", line))]
            memory = [line for line in stack if re.search(r"\[(?:sp|x29)\b", line)]
            vector = [line for line in memory if re.search(r"\b[qdsbhv]\d+(?:\.[0-9]+[bhsd])?\b", line)]
            conditional = [line for line in body if re.match(r"^(?:b\.|cbz |cbnz |tbz |tbnz )", line)]
            indirect = [line for line in body if re.match(r"^(br|blr) ", line)]
            jumps = [line for line in body if re.match(r"^(b|br) ", line)]
        else:
            frame = any(re.match(r"movq? %rsp, %rbp", line) for line in body)
            stack = [line for line in body if re.search(r"%rsp\b", line) or
                     (frame and re.search(r"%rbp\b", line)) or re.match(r"^(push|pop)(q|l|w)? ", line)]
            memory = [line for line in stack if not line.startswith(("lea", "nop")) and
                      (re.search(r"\([^)]*%(?:rsp|rbp)\b", line) or re.match(r"^(push|pop)(q|l|w)? ", line))]
            vector = [line for line in memory if re.search(r"%[xyz]mm\d+\b", line)]
            conditional = [line for line in body if line.startswith("j") and not line.startswith(("jmp ", "jmpq "))]
            indirect = [line for line in body if re.match(r"^(callq?|jmpq?) \*", line)]
            jumps = [line for line in body if re.match(r"^(jmp|jmpq) ", line)]
        hints = [line for line in body if line.startswith(("prfm ", "prefetch"))]
        result.append({"name": name, "role": kind, "text_bytes": sizes.get(name),
                       "static_instruction_sites": len(body), "ordinary_calls": calls,
                       "accidental_recurring_helper_calls": [line for line in calls if HELPER_CALL.search(line)] if kind in RECURRING else [],
                       "stack_related_sites": stack, "stack_memory_sites": memory,
                       "vector_stack_memory_sites": vector,
                       "prefetch_sites": hints, "conditional_branch_sites": conditional,
                       "indirect_transfer_sites": indirect, "jump_sites": jumps,
                       "first": body[:12], "last": body[-12:]})
    size_text = output(["llvm-size-21", "--format=sysv", str(obj)])
    obj.with_suffix(".size").write_text(size_text)
    sections = {fields[0]: int(fields[1]) for line in size_text.splitlines()
                if len(fields := line.split()) >= 3 and fields[0].startswith(".text") and fields[1].isdigit()}
    return {"object_bytes": obj.stat().st_size, "text_bytes": sum(sections.values()), "functions": result}


def inspect_ir(path):
    bodies, current = {}, None
    for line in path.read_text().splitlines():
        if line.startswith("define "):
            match = re.search(r'@(?:"([^"]+)"|([^ (]+))\(', line)
            current = match[1] or match[2]
            bodies[current] = {"definition": line, "musttail": [], "argument_stores": []}
        elif current:
            if "musttail call" in line:
                bodies[current]["musttail"].append(line.strip())
            if re.search(r"\bstore (?:ptr|i32|i64) ", line):
                bodies[current]["argument_stores"].append(line.strip())
            if line == "}":
                current = None
    names = list(bodies)
    demangled = subprocess.run(["llvm-cxxfilt-21"], input="\n".join(names) + "\n",
                              text=True, capture_output=True, check=True).stdout.splitlines()
    return [{"name": name, **bodies[mangled]} for mangled, name in zip(names, demangled)
            if "preserve_nonecc" in bodies[mangled]["definition"] and
            (role(name) in RECURRING or role(name) in {"ring_yield", "ring_retirement"})]


def compact(function):
    return {"name": function["name"], "role": function["role"], "text_bytes": function["text_bytes"],
            "static_instruction_sites": function["static_instruction_sites"],
            **{name: len(function[name]) for name in ("ordinary_calls", "stack_related_sites", "stack_memory_sites",
                "vector_stack_memory_sites", "prefetch_sites", "conditional_branch_sites", "indirect_transfer_sites", "jump_sites")}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root, destination = args.source.resolve(), args.output.resolve()
    destination.mkdir(parents=True, exist_ok=False)
    (destination / "recipe.py").write_bytes(Path(__file__).read_bytes())
    source = root / "workbench/spikes/streampack/implementation_loom.cpp"
    compiler = output(["clang++-21", "--version"])
    if not re.search(r"\b21\.1\.8\b", compiler):
        raise ValueError("Requires pinned Clang 21.1.8")
    common = ["-std=c++23", "-O3", "-g0", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
              "-fno-vectorize", "-fno-slp-vectorize", "-fno-fast-math", "-ffp-contract=off",
              "-fdenormal-fp-math=ieee", "-fvisibility=hidden", "-fvisibility-inlines-hidden",
              "-ffunction-sections", "-fdata-sections", "-I" + str(root / "ikea/include"),
              "-I" + str(root / "loom/include"), "-I" + str(root / "orbital/include")]
    cross = ["--target=x86_64-linux-gnu", "--sysroot=/usr/x86_64-linux-gnu",
             "-isystem", "/usr/x86_64-linux-gnu/include/c++/15",
             "-isystem", "/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu"]
    targets = {"v2": ("arm", ["-mcpu=neoverse-v2", "-mtune=neoverse-v2", "-DSIXDB_TUNE_NEOVERSE_V2=1"]),
               "zen5": ("x86", cross + ["-march=znver5", "-mtune=znver5", "-DSIXDB_TUNE_ZEN5=1"]),
               "granite-rapids": ("x86", cross + ["-march=graniterapids", "-mtune=graniterapids", "-DSIXDB_TUNE_GRANITE_RAPIDS=1"])}
    receipt = {"compiler": compiler, "recipe_sha256": sha(Path(__file__)), "source": str(source),
               "targets": {}, "limits": ["Actual consumer TU, no LTO, both auto-vectorizers disabled; explicit Ikea intrinsics remain enabled.",
                   "Compile-only objects, no hardware timing or PMU claims.",
                   "Instruction/call/branch/hint/stack counts are static sites across all paths, not executed counts or cycles.",
                   "Stack-memory sites include explicit push/pop and register save/restore, exclude address-only LEA/NOP and implicit call/return effects; scalar scratch is reported, not assumed absent.",
                   "Current source and all transitive system/project dependencies are hashed before/after each compile and rechecked across all targets.",
                   "The saved bitcode/IR is the real Clang -save-temps frontend output; it records mandatory tail transfers before backend code generation."]}
    baseline, summary, failures = {}, {}, []
    for target, (arch, flags) in targets.items():
        folder = destination / target
        folder.mkdir()
        obj = folder / "implementation_loom.o"
        dep = obj.with_suffix(".d")
        before_dep = folder / "before.d"
        prefix = ["clang++-21", *common, *flags]
        dependency_command = [*prefix, "-M", "-MF", str(before_dep), str(source)]
        subprocess.run(dependency_command, check=True)
        before = {str(path): sha(path) for path in dependencies(before_dep, root)}
        for path, digest in before.items():
            if path in baseline and baseline[path] != digest:
                raise RuntimeError("Dependency changed between targets: " + path)
            baseline[path] = digest
        command = [*prefix, "-save-temps=obj", "-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
        run = subprocess.run(command, capture_output=True, text=True)
        obj.with_suffix(".log").write_text(run.stdout + run.stderr)
        entry = {"argv": command, "dependency_argv": dependency_command, "returncode": run.returncode,
                 "dependencies_before_sha256": before, "log_sha256": sha(obj.with_suffix(".log"))}
        receipt["targets"][target] = entry
        if run.returncode:
            (destination / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
            raise RuntimeError(run.stderr)
        after = {str(path): sha(path) for path in dependencies(dep, root)}
        changed = [path for path in sorted(set(before) | set(after)) if before.get(path) != after.get(path)]
        entry["dependencies_after_sha256"] = after
        entry["changed_during_compile"] = changed
        if changed:
            (destination / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
            raise RuntimeError("Dependency changed during compile: " + ", ".join(changed))
        bc, ir = obj.with_suffix(".bc"), obj.with_suffix(".ll")
        subprocess.run(["llvm-dis-21", str(bc), "-o", str(ir)], check=True)
        entry.update(inspect(obj, arch))
        entry.update({"object_sha256": sha(obj), "depfile_sha256": sha(dep),
                      "bitcode_sha256": sha(bc), "ir_sha256": sha(ir), "ir": inspect_ir(ir)})
        callbacks = [f for f in entry["functions"] if f["role"] in RECURRING]
        ir_callbacks = [f for f in entry["ir"] if role(f["name"]) in RECURRING]
        violations = [{"name": f["name"], "ordinary_calls": f["accidental_recurring_helper_calls"]}
                      for f in callbacks if f["accidental_recurring_helper_calls"]]
        violations += [{"name": f["name"], "missing_musttail": True}
                       for f in ir_callbacks if not f["musttail"]]
        entry["observed_callback_counts"] = {"object": len(callbacks), "ir": len(ir_callbacks)}
        if not callbacks or not ir_callbacks:
            violations.append({"missing_concrete_callbacks": True})
        entry["contract_violations"] = violations
        failures += [{"target": target, **v} for v in violations]
        summary[target] = [compact(f) for f in entry["functions"]]
        counts = {kind: len([f for f in callbacks if f["role"] == kind]) for kind in sorted(RECURRING)}
        print(f"{target}: {counts}; {len(violations)} contract violations; {len(before)} stable dependencies", flush=True)
        (destination / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        (destination / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    changed = [path for path, digest in baseline.items() if sha(Path(path)) != digest]
    receipt["changed_before_completion"] = changed
    receipt["passed"] = not failures and not changed
    receipt["contract_violations"] = failures
    (destination / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if not receipt["passed"]:
        raise RuntimeError("Qualification failed; inspect receipt contract/source violations")
    print("PASS: actual V2/Zen5/Granite Rapids callbacks retain musttail and no ordinary hint/kernel helper calls")


if __name__ == "__main__":
    main()
