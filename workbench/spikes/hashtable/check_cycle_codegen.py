#!/usr/bin/env python3
"""Inspect complete read consumers; clocks and cold contract failures are allowed."""
import argparse
import gzip
import json
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('assembly', type=Path)
a = p.parse_args()
source = gzip.open(a.assembly, 'rt').read() if a.assembly.suffix == '.gz' else a.assembly.read_text()
functions = {};name = None
for line in source.splitlines():
    match = re.match(r'^[0-9a-f]+ <(.*)>:', line)
    if match:
        name = match[1]
        if 'ht::cycle::run::reads<' in name:functions[name] = []
    elif name in functions:functions[name].append(line)
assert len(functions) >= 3, 'candidate and both baseline complete consumers'
out = []
for name, lines in functions.items():
    calls = [s.strip() for s in lines if re.search(r'\b(?:bl|blr|callq?|call)\s', s)]
    other = [s for s in calls if not any(x in s for x in ['steady_clock', 'fprintf', 'abort'])]
    assert not other, (name, other)
    out.append({'consumer': name, 'calls': calls, 'lines': len(lines)})
print(json.dumps({'consumers': out, 'outlined_read_calls': 0,
    'limitation': 'Static complete-function call inspection, not a profile or frame/traffic attribution.'}, indent=2))
