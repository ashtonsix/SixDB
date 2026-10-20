#!/usr/bin/env python3
"""Retain complete-consumer codegen and straight-line scheduling witnesses."""
import argparse
import collections
import json
import pathlib
import re
import shlex
import subprocess

p = argparse.ArgumentParser()
p.add_argument("build", type=pathlib.Path)
p.add_argument("out", type=pathlib.Path)
p.add_argument("profile")
p.add_argument("cpu")
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
commands = json.loads((a.build / "compile_commands.json").read_text())
c = next(c for c in commands if c["file"].endswith("/boundlist/codegen.cpp"))
args = shlex.split(c["command"])
i = args.index("-o")
del args[i:i + 2]
args.remove("-c")
assembly = a.out / (a.profile + "-kernels.s")
subprocess.run(args + ["-S", "-o", str(assembly.resolve())], cwd=c["directory"], check=True)
obj = a.build / "workbench/spikes/boundlist/CMakeFiles/boundlist_codegen.dir/codegen.cpp.o"
dis = subprocess.check_output(["llvm-objdump-21", "-d", "--no-show-raw-insn", str(obj)], text=True)
(a.out / (a.profile + "-kernels.txt")).write_text(dis)
text = assembly.read_text().splitlines()
report = {"scope": "Static instructions and LLVM scheduling estimates are not native latency/throughput. MCA covers full readable local groups, not branching search or cache misses.", "witnesses": {}}
for name in ("bl_prefix8_pair", "bl_prefix16_pair", "bl_direct16_pair", "bl_checkpoint16_find", "bl_byte_owner16", "bl_byte_owner32"):
    start = next(i for i, line in enumerate(text) if line.startswith(name + ":"))
    end = next(i for i in range(start + 1, len(text)) if text[i].startswith(".Lfunc_end"))
    body = "\n".join(line for line in text[start + 1:end] if not line.strip().startswith((".cfi", ".size"))) + "\n"
    r = subprocess.run(["llvm-mca-21", "-mcpu=" + a.cpu, "-iterations=100"], input=body, text=True, capture_output=True)
    (a.out / (a.profile + "-" + name + "-mca.txt")).write_text(r.stdout + r.stderr)
    report["witnesses"][name] = {"returncode": r.returncode, "summary": r.stdout.split("Instruction Info:")[0].strip()}

exe = a.build / "workbench/spikes/boundlist/boundlist_bench"
full = subprocess.check_output(["llvm-objdump-21", "-d", "--demangle", "--no-show-raw-insn", str(exe)], text=True)
(a.out / (a.profile + "-assembly.txt")).write_text(full)
consumers = []
for body in re.split(r"\n[0-9a-f]+ <", full):
    name = body.splitlines()[0] if body else ""
    if not name.startswith("unsigned long consume<") or not any(t in name for t in ("candidate<", "geometric<", "fused<", "current_stream<", "unique_point<", "batched<")):
        continue
    calls = collections.Counter(re.findall(
        r"^[ \t]*[0-9a-f]+:[ \t]+(?:bl|blr|call|callq)[ \t]+[^<\n]*<([^\n]*)", body, re.MULTILINE))
    consumers.append({"function": name, "calls": dict(calls)})
report["complete_consumers"] = consumers
packed = a.build / "workbench/spikes/boundlist/boundlist_packed"
packed_consumers = []
if packed.exists():
    full_packed = subprocess.check_output(["llvm-objdump-21", "-d", "--demangle", "--no-show-raw-insn", str(packed)], text=True)
    (a.out / (a.profile + "-packed-assembly.txt")).write_text(full_packed)
    for body in re.split(r"\n[0-9a-f]+ <", full_packed):
        name = body.splitlines()[0] if body else ""
        if not name.startswith("unsigned long consumer<") or "99u>" in name:
            continue
        calls = collections.Counter(re.findall(
            r"^[ \t]*[0-9a-f]+:[ \t]+(?:bl|blr|call|callq)[ \t]+[^<\n]*<([^\n]*)", body, re.MULTILINE))
        packed_consumers.append({"function": name, "calls": dict(calls)})
    assert len(packed_consumers) == 112, len(packed_consumers)
    report["packed_consumers"] = packed_consumers
(a.out / (a.profile + "-codegen.json")).write_text(json.dumps(report, indent=2) + "\n")
assert len(consumers) >= 80, len(consumers)
# Batch stage calls may retain a single shared frame. Ordinary independent
# lanes must not be serialized through a per-query helper or runtime memcpy.
bad = [c for c in consumers if "batched<" not in c["function"] and c["calls"]]
assert not bad, bad[:3]
assert not any(c["calls"] for c in packed_consumers)
assert all(w["returncode"] == 0 for w in report["witnesses"].values())
print(f"{len(consumers)} complete consumers and {len(packed_consumers)} packed consumers audited; ordinary lanes have no helper calls; {len(report['witnesses'])} local scheduling witnesses retained")
