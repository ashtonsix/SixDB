#!/usr/bin/env python3
import collections
import json
import pathlib
import re
import subprocess
import sys

exe, out = map(pathlib.Path, sys.argv[1:])
text = subprocess.check_output(["llvm-objdump-21", "-d", "--demangle", "--no-show-raw-insn", str(exe)], text=True)
out.write_text(text)
consumers = []
for body in re.split(r"\n[0-9a-f]+ <", text):
    name = body.splitlines()[0] if body else ""
    ordinary = name.startswith(("unsigned long consume<", "unsigned long chase<", "unsigned long projection<", "unsigned long carried_range<", "unsigned long pack<", "unsigned long direct<", "unsigned long manual<"))
    callback = re.match(r"(?:void )?native<.*>::(?:header|metadata|payload)\(", name)
    if not ordinary and not callback:
        continue
    if ", 99u>" in name or "panel(" in name or "lambda(unsigned int)>" in name:
        continue
    calls = collections.Counter(re.findall(r"^[ \t]*[0-9a-f]+:[ \t]+(?:bl|blr|call|callq)[ \t]+[^<\n]*<([^\n]*)", body, re.MULTILINE))
    calls = {k: v for k, v in calls.items() if "__assert_fail" not in k}
    allowed = {}
    if exe.name in ("boundlist_mutation", "boundlist_packing"):
        allowed = {k: v for k,v in calls.items() if re.match(r"^(?:memcpy|memmove|memset)(?:@|>)",k)}
        calls = {k: v for k,v in calls.items() if k not in allowed}
    consumers.append({"function": name, "calls": calls, "memory_calls": allowed})
assert consumers
report = {"executable": exe.name, "consumers": consumers}
out.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
bad = [x for x in consumers if x["calls"]]
assert not bad, [{"function":x["function"][:180],"calls":[c[:180] for c in x["calls"]]} for x in bad[:3]]
print(f"{exe.name}: {len(consumers)} consumers have no ordinary kernel helper calls")
