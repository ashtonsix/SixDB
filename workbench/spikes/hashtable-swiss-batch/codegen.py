#!/usr/bin/env python3
"""Emit complete callers, record batch deltas, require primary-path equality."""
import argparse
import difflib
import json
import re
import shlex
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
entries = json.loads((a.build / 'compile_commands.json').read_text())
functions, commands = {}, []
variants = sorted({re.search(r'swiss_codegen_(\w+)\.dir', e['command'])[1]
                   for e in entries if e['file'].endswith('/hashtable-swiss-batch/codegen.cpp')})
for variant in variants:
    entry, = [e for e in entries if e['file'].endswith('/hashtable-swiss-batch/codegen.cpp')
              and f'swiss_codegen_{variant}.dir' in e['command']]
    command = shlex.split(entry['command'])
    at = command.index('-o')
    del command[at:at + 2]
    command.remove('-c')
    output = (a.output / f'{variant}.s').resolve()
    command += ['-DNDEBUG', '-S', '-o', str(output)]
    commands.append({'variant': variant, 'directory': entry['directory'], 'command': command})
    subprocess.run(command, cwd=entry['directory'], check=True)
    found = {}
    for match in re.finditer(r'^(overflow_(?:read|write_mixed|write_existing|erase|modify)_k\d+_v\d+):.*?^\.Lfunc_end\d+:', output.read_text(), re.M | re.S):
        lines = []
        for line in match.group().splitlines()[1:-1]:
            line = line.split('#', 1)[0].strip()
            if not line or (line.startswith('.') and not line.endswith(':')):
                continue
            line = re.sub(r'\.LBB\d+_', '.LBB_', line)
            lines.append(re.sub(r'\.LCPI\d+_', '.LCPI_', line))
        found[match[1]] = lines
    if len(found) != 28:
        raise RuntimeError(f'{variant}: expected 28 complete read/upsert/erase/RMW loops')
    functions[variant] = found
checks, diffs = [], []
for variant in [v for v in variants if v != 'group16']:
    for name, baseline in functions['group16'].items():
        candidate = functions[variant][name]
        checks.append({'variant': variant, 'function': name, 'identical': baseline == candidate,
                       'baseline_lines': len(baseline), 'candidate_lines': len(candidate)})
        if baseline != candidate:
            diffs.extend(difflib.unified_diff(baseline, candidate, fromfile='group16/' + name,
                                             tofile=variant + '/' + name, lineterm=''))
(a.output / 'comparison.json').write_text(json.dumps(checks, indent=2) + '\n')
(a.output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
(a.output / 'diff.txt').write_text('\n'.join(diffs) + '\n')
if not all(c['identical'] for c in checks ):
    raise RuntimeError('batch experiment changed primary callers; inspect codegen/diff.txt')
