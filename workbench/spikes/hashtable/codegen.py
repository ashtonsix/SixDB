#!/usr/bin/env python3
"""Compile the experiment's exact codegen TU to assembly and model straight-line predicates."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cpu', required=True)
p.add_argument('--triple', required=True)
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=True)
commands = json.loads((a.build / 'compile_commands.json').read_text())
entry = next(c for c in commands if c['file'].endswith('/hashtable/codegen.cpp'))
cmd = shlex.split(entry['command'])
cmd[cmd.index('-o') + 1] = str(a.output / 'codegen.s')
cmd[cmd.index('-c')] = '-S'
subprocess.run(cmd, cwd=entry['directory'], check=True)
assembly = (a.output / 'codegen.s').read_text()
receipt = {'compile': cmd, 'cpu_model': a.cpu, 'triple': a.triple,
           'scope': 'llvm-mca scheduling model of straight-line predicates; not measured latency or memory performance',
           'predicates': {}}
for name in ('fp3_decode', 'fp3_planes', 'fp7_decode', 'fp7_planes', 'fp8_decode'):
    symbol = 'hashtable_' + name
    match = re.search(r'^' + symbol + r':.*?^\.Lfunc_end\d+:', assembly, re.M | re.S)
    if not match:
        raise RuntimeError('missing assembly symbol ' + symbol)
    body = match.group(0).rsplit('\n', 1)[0]
    # Debug and unwind directives aren't instructions and may refer outside the
    # selected function. Keep local branch labels if a future predicate adds one.
    lines = [line for line in body.splitlines() if not re.match(r'\s*\.(loc|cfi_|file)',line)]
    path = a.output / (name + '.s')
    path.write_text('\n'.join(lines) + '\n')
    call = ['llvm-mca-21', '-mtriple=' + a.triple, '-mcpu=' + a.cpu,
            '-iterations=100', str(path)]
    result = subprocess.run(call, check=True, capture_output=True, text=True)
    (a.output / (name + '.mca.txt')).write_text(result.stdout)
    (a.output / (name + '.stderr.txt')).write_text(result.stderr)
    receipt['predicates'][name] = {'command': call, 'summary': result.stdout.split('Instruction Info:')[0]}
(a.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
