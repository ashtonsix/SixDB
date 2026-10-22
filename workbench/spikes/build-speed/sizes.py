#!/usr/bin/env python3
"""Section sizes (text/data/debug/other) for every ELF output under a build dir."""
import csv, subprocess, sys
from pathlib import Path
build, out = Path(sys.argv[1]), sys.argv[2]
rows = []
for p in build.rglob('*'):
    if not p.is_file() or 'CMakeFiles/CMakeScratch' in str(p) or 'CompilerId' in str(p): continue
    with open(p, 'rb') as f:
        if f.read(4) != b'\x7fELF' and p.suffix != '.a': continue
    r = subprocess.run(['llvm-size-21', '-A', str(p)], capture_output=True, text=True)
    if r.returncode: continue
    s = {'text': 0, 'data': 0, 'debug': 0, 'other': 0}
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[1].isdigit(): continue
        name, n = parts[0], int(parts[1])
        k = 'debug' if name.startswith('.debug') else 'text' if name.startswith('.text') else \
            'data' if name.startswith(('.rodata', '.data', '.bss', '.eh_frame', '.gcc_except')) else 'other'
        s[k] += n
    rows.append({'path': str(p.relative_to(build)), 'kind': 'obj' if p.suffix == '.o' else 'lib' if p.suffix == '.a' else 'exe',
                 'file_bytes': p.stat().st_size, **s})
rows.sort(key=lambda r: -r['file_bytes'])
with open(out, 'w', newline='') as f:
    w = csv.DictWriter(f, list(rows[0])); w.writeheader(); w.writerows(rows)
