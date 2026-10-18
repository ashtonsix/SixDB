#!/usr/bin/env python3
"""Compare complete consumer instructions with an external captured baseline."""
import argparse
import difflib
import json
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--baseline", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--cpu", choices=("zen5", "granite-rapids", "neoverse-v2"), required=True)
p.add_argument("--isa", choices=("native", "avx2"), default="native")
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
a.output.mkdir(parents=True, exist_ok=True)
march, tune, definition = {
    "zen5": ("znver5", "znver5", "ZEN5"),
    "granite-rapids": ("graniterapids", "graniterapids", "GRANITE_RAPIDS"),
    "neoverse-v2": ("armv8-a+simd", "neoverse-v2", "NEOVERSE_V2"),
}[a.cpu]
if a.isa == "avx2":
    assert a.cpu != "neoverse-v2"
    march = "x86-64-v3"
commands = []
for name, source in (("before", a.baseline.resolve()), ("after", root)):
    obj = a.output / f"{name}.o"
    cmd = ["clang++-21", "-std=c++23", "-O3", "-DNDEBUG", f"-march={march}",
           f"-mtune={tune}", f"-DSIXDB_TUNE_{definition}=1", "-mllvm",
           "-inline-threshold=1000", "-falign-functions=64", "-I", str(source / "ikea/include"),
           "-I", str(source), f"-DIKEA_BEFORE_ACCUMULATION={int(name == 'before')}",
           "-c", str(root / "workbench/benchmarks/hashtable/codegen.cpp"), "-o", str(obj)]
    subprocess.run(cmd, check=True)
    commands.append(cmd)
    with (a.output / f"{name}.asm").open("w") as out:
        subprocess.run(["llvm-objdump-21", "-d", "--demangle", "--no-show-raw-insn", str(obj)],
                       check=True, stdout=out)


def functions(path):
    out, name = {}, None
    for line in path.read_text().splitlines():
        m = re.match(r"^([0-9a-f]+) <(.+)>:", line)
        if m:
            name = m[2]
            out[name] = []
        elif name and re.match(r"^\s+[0-9a-f]+:", line):
            out[name].append(line)
    return out


def normalize(lines):
    addresses = {int(re.match(r"\s*([0-9a-f]+):", line)[1], 16): i
                 for i, line in enumerate(lines)}
    out = []
    for line in lines:
        text = re.sub(r"^\s*[0-9a-f]+:\s*", "", line)
        text = re.sub(r"#.*", "", text).strip()
        text = re.sub(r"\b0x([0-9a-f]+)\b(?=\s*<)",
                      lambda m: "@" + str(addresses.get(int(m[1], 16), "external")), text)
        text = re.sub(r"-?0x[0-9a-f]+\(%rip\)", "@constant(%rip)", text)
        text = re.sub(r"<.*>", "", text).strip()
        if not text.startswith(("nop", "data16", "cs ")):
            out.append(text)
    return out


before = functions(a.output / "before.asm")
after = functions(a.output / "after.asm")
names = [name for name in before if name.startswith(("read_k", "write_k", "grow_k", "accumulate_k", "modify_k"))]
assert len(names) == 21 and all(name in after for name in names)
rows = []
for name in names:
    old, new = normalize(before[name]), normalize(after[name])
    rows.append(dict(name=name, identical=old == new, before_instructions=len(old),
                     after_instructions=len(new), diff=list(difflib.unified_diff(old, new))))
outlined=[]
for name, old_lines in before.items():
    if not any(tag in name for tag in ("accumulate_points<","accumulate_images<","accumulate_swiss_pipelined<","put_with_store<")):
        continue
    mapped=name.replace("merge_study::merge_status","ikea::hashtable::accumulation_status").replace("merge_study::","ikea::hashtable::detail::accumulation::")
    assert mapped in after,("Missing outlined accumulation body",name)
    old,new=normalize(old_lines),normalize(after[mapped])
    outlined.append(dict(before_name=name,after_name=mapped,identical=old==new,
        before_instructions=len(old),after_instructions=len(new),diff=list(difflib.unified_diff(old,new))))
assert len(outlined)>=6
result = dict(cpu=a.cpu, isa=a.isa, commands=commands, outlined=outlined,
              normalization="Addresses, RIP-relative constant addresses and NOPs; not a semantic proof of constants/callees",
              consumers=rows)
(a.output / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"{a.cpu}/{a.isa}: {sum(row['identical'] for row in rows)}/{len(rows)} identical entry instruction streams; "
      f"{sum(row['identical'] for row in outlined)}/{len(outlined)} identical outlined accumulation bodies")
