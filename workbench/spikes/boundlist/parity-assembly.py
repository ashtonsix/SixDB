#!/usr/bin/env python3
"""Compare matched old/current reader instruction text, excluding relocation targets/padding.

This is a codegen diagnostic, not semantic proof or a performance model. Source
controls and native measurements independently establish correctness and cost.
"""
import json
import pathlib
import re
import sys

source, output = map(pathlib.Path, sys.argv[1:])
pairs = {}
for body in re.split(r'\n[0-9a-f]+ <', source.read_text()):
    name = body.splitlines()[0] if body else ''
    match = re.search(r'^unsigned long (?:chase|projection)<.*?reader<([04])u,', name)
    if not match:
        continue
    key = re.sub(r'reader<[04]u,', 'reader<X,', name)
    instructions = []
    for line in body.splitlines()[1:]:
        instruction = re.match(r'\s*[0-9a-f]+:\s*(.*)', line)
        if not instruction:
            continue
        text = instruction[1]
        # Keep immediates. Only annotated addresses/PC-relative references are
        # relocations; removing every hex number would hide real differences.
        text = re.sub(r'0x[0-9a-f]+ <.*>|\b[0-9a-f]+ <.*>', 'TARGET', text)
        text = re.sub(r'[-+]?0x[0-9a-f]+\(%rip\)(\s*#.*)?', 'RELOCATION(%rip)', text)
        text = re.sub(r'\s+#\s+.*$', '', text)
        text = ' '.join(text.split())
        if text.startswith(('nop', 'data16', 'udf')):
            continue
        instructions.append(text)
    pairs.setdefault(key, {})[match[1]] = instructions
rows = []
for name, values in pairs.items():
    assert len(values) == 2, name
    rows.append(dict(function=name, same_sequence=values['0'] == values['4'],
                     current_instructions=len(values['0']), before_instructions=len(values['4'])))
assert rows
output.write_text(json.dumps({'scope': __doc__, 'consumers': rows}, indent=2)+'\n')
print(f'{source.name}: {sum(r["same_sequence"] for r in rows)}/{len(rows)} matched normalized sequences')
