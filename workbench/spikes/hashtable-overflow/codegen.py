#!/usr/bin/env python3
"""Compare emitted complete read loops; save every changed instruction."""
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
functions = {}
commands = []
for variant in ['baseline', 'tag8', 'filter15']:
    entry, = [e for e in entries if e['file'].endswith('/hashtable-overflow/codegen.cpp')
              and f'overflow_codegen_{variant}.dir' in e['command']]
    command = shlex.split(entry['command'])
    at = command.index('-o')
    del command[at:at + 2]
    command.remove('-c')
    output = (a.output / f'{variant}.s').resolve()
    command += ['-DNDEBUG', '-S', '-o', str(output)]
    commands.append({'variant': variant, 'directory': entry['directory'], 'command': command})
    subprocess.run(command, cwd=entry['directory'], check=True)
    text = output.read_text()
    found = {}
    for match in re.finditer(r'^(overflow_(?:read|write_mixed|write_existing)_k\d+_v\d+):.*?^\.Lfunc_end\d+:', text, re.M | re.S):
        lines = []
        for line in match.group().splitlines()[1:-1]:
            line = line.split('#', 1)[0].strip()
            if not line or (line.startswith('.') and not line.endswith(':')):
                continue
            line = re.sub(r'\.LBB\d+_', '.LBB_', line)
            line = re.sub(r'\.LCPI\d+_', '.LCPI_', line)
            lines.append(line)
        found[match[1]] = lines
    if len(found) != 18:
        raise RuntimeError(f'{variant}: expected eighteen complete read/write loops, got {list(found)}')
    functions[variant] = found
checks = []
diffs = []
for variant in ['tag8', 'filter15']:
    for name, baseline in functions['baseline'].items():
        candidate = functions[variant][name]
        same = baseline == candidate
        checks.append({'variant': variant, 'function': name, 'identical': same,
                       'baseline_lines': len(baseline), 'candidate_lines': len(candidate)})
        if not same:
            diffs.extend(difflib.unified_diff(baseline, candidate, fromfile='baseline/' + name,
                                             tofile=variant + '/' + name, lineterm=''))
(a.output / 'comparison.json').write_text(json.dumps(checks, indent=2) + '\n')
(a.output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
(a.output / 'diff.txt').write_text('\n'.join(diffs) + '\n')
if not all(c['identical'] for c in checks):
    raise RuntimeError('complete primary read loops changed; inspect codegen/diff.txt before timing')
