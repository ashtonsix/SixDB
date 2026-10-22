#!/usr/bin/env python3
"""Recompile selected TUs under flag variants; record wall, peak RSS and sections.

usage: variants.py BUILD OUT.csv JOBS SOURCE...
"""
import concurrent.futures as cf, csv, json, os, re, resource, shlex, subprocess, sys, tempfile, time
from pathlib import Path

VARIANTS = {
    'base': [],
    'gline': ['-gline-tables-only'],
    'g0': ['-g0'],
    'noat': ['-Xclang', '-fexperimental-assignment-tracking=disabled'],
    'O1': ['-O1'],
    'O1_gline': ['-O1', '-gline-tables-only'],
    'O0': ['-O0'],
    'Og': ['-Og'],
}
build, out, jobs, sources = Path(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4:]
db = json.loads((build / 'compile_commands.json').read_text())
tmp = Path(tempfile.mkdtemp())

def entry(src):
    m = [e for e in db if e['file'].endswith(src)]
    assert len(m) >= 1, src
    return m[0]

def sections(obj):
    s = {'text': 0, 'debug': 0}
    for line in subprocess.run(['llvm-size-21', '-A', obj], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) >= 2 and p[1].isdigit():
            if p[0].startswith('.text'): s['text'] += int(p[1])
            elif p[0].startswith('.debug'): s['debug'] += int(p[1])
    return s

def run(src, name):
    e = entry(src)
    args = shlex.split(e['command'])
    i = args.index('-o'); obj = str(tmp / f'{name}-{Path(src).name}.o'); args[i + 1] = obj
    args = [a for a in args if a not in ('-MD',) ] 
    if '-MF' in args:
        j = args.index('-MF'); del args[j:j + 2]
    args = args[:1] + args[1:] + VARIANTS[name]  # later flags win
    t0 = time.perf_counter()
    p = subprocess.Popen(['/usr/bin/time', '-f', '%M', '-o', obj + '.rss', *args], cwd=e['directory'],
                         stderr=subprocess.DEVNULL)
    p.wait()
    wall = time.perf_counter() - t0
    rss = int(Path(obj + '.rss').read_text().split()[-1]) if p.returncode == 0 else 0
    s = sections(obj) if p.returncode == 0 else {'text': 0, 'debug': 0}
    if os.path.exists(obj): os.remove(obj)
    return {'source': src, 'variant': name, 'status': p.returncode, 'wall_s': round(wall, 2),
            'max_rss_kib': rss, **s}

with cf.ThreadPoolExecutor(jobs) as ex:
    rows = list(ex.map(lambda a: run(*a), [(s, v) for s in sources for v in VARIANTS]))
with open(out, 'w', newline='') as f:
    w = csv.DictWriter(f, list(rows[0])); w.writeheader(); w.writerows(rows)
