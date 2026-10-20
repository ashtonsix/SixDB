#!/usr/bin/env python3
"""Check the isolated and complete search consumers, not just tiny witnesses."""
import collections
import json
import pathlib
import re
import sys

text = pathlib.Path(sys.argv[1]).read_text()
consumers = []
for body in re.split(r"\n[0-9a-f]+ <", text):
    name = body.splitlines()[0] if body else ""
    if not name.startswith("unsigned long consume<"):
        continue
    calls = collections.Counter(re.findall(
        r"^[ \t]*[0-9a-f]+:[ \t]+(?:bl|blr|call|callq)[ \t]+[^<\n]*<([^\n]*)", body, re.MULTILINE))
    consumers.append({"function": name, "calls": dict(calls)})
assert len(consumers) >= 162, len(consumers)
assert not any(c["calls"] for c in consumers), [c for c in consumers if c["calls"]][:3]
print(json.dumps({"consumers": consumers, "helper_calls": 0}, indent=2))
