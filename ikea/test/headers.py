#!/usr/bin/env python3
"""Compile each ordinary/author header independently using the configured profile."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
args = parser.parse_args()
entries = json.loads((args.build / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('/ikea/examples/seriespack/ordinary.cpp'))
parts = entry.get('arguments') or shlex.split(entry['command'])
command = []
skip = False
for part in parts:
    if skip:
        skip = False
        continue
    if part in ('-o', '-c', '-MF', '-MT', '-MQ'):
        skip = True
        continue
    if part in ('-MD', '-MMD'):
        continue
    command.append(part)
command += ['-fsyntax-only', '-x', 'c++', '-']
include = Path(__file__).resolve().parents[1] / 'include'
headers = sorted(p for p in include.rglob('*.h') if 'detail' not in p.parts)
for header in headers:
    name = header.relative_to(include)
    result = subprocess.run(command, input=f'#include <{name}>\n', text=True,
                            cwd=entry['directory'], capture_output=True)
    if result.returncode:
        raise SystemExit(f'{name}\n{result.stderr}')
print(f'Ikea: {len(headers)} ordinary/author headers compile independently')
