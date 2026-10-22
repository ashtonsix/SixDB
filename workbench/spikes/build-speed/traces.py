#!/usr/bin/env python3
"""Summarize Clang -ftime-trace output under a build directory.

Writes tu.csv (per-TU phase totals), headers.csv (inclusive parse time per
header summed over TUs), templates.csv (instantiation time by normalized
template name), passes.csv (backend pass totals) and copies the slowest traces.
"""
import collections, csv, gzip, json, re, shutil, sys
from pathlib import Path

build, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
root = str(Path.cwd()) + '/'
tus, headers, header_tus, templates, template_full, passes = [], collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()

def norm(name):
    depth, s = 0, []
    for ch in name:
        if ch == '<':
            if depth == 0: s.append('<>')
            depth += 1
        elif ch == '>':
            depth = max(0, depth - 1)
        elif depth == 0:
            s.append(ch)
    return ''.join(s)

for trace in build.rglob('*.json'):
    if not trace.name.endswith('.cpp.json'): continue
    try: events = json.loads(trace.read_text())['traceEvents']
    except Exception: continue
    totals = {e['name']: e['dur'] for e in events if e.get('name', '').startswith('Total ') and 'dur' in e}
    seen = set()
    for e in events:
        if e.get('ph') != 'X': continue
        n, d, det = e['name'], e.get('dur', 0), e.get('args', {}).get('detail', '')
        if n == 'Source':
            h = det.replace(root, '')
            headers[h] += d
            if h not in seen: header_tus[h] += 1; seen.add(h)
        elif n in ('InstantiateFunction', 'InstantiateClass'):
            templates[(n, norm(det))] += d
            template_full[(n, det[:300])] += d
        elif n in ('RunPass', 'OptFunction', 'CodeGenPasses', 'OptModule', 'CodeGen Function'):
            passes[(n, det if n == 'RunPass' else '')] += d
    tus.append({'tu': str(trace.relative_to(build)), **{k: totals.get('Total ' + k, 0) // 1000 for k in (
        'ExecuteCompiler', 'Frontend', 'Backend', 'Source', 'ParseClass', 'InstantiateFunction',
        'InstantiateClass', 'PerformPendingInstantiations', 'CodeGen Function', 'OptModule',
        'CodeGenPasses', 'DebugType', 'DebugFunction', 'DebugGlobalVariable')}, 'trace': trace})

tus.sort(key=lambda t: -t['ExecuteCompiler'])
with open(out / 'tu.csv', 'w', newline='') as f:
    w = csv.DictWriter(f, [k for k in tus[0] if k != 'trace']); w.writeheader()
    for t in tus: w.writerow({k: v for k, v in t.items() if k != 'trace'})
def dump(name, counter, cols, n=400):
    with open(out / name, 'w', newline='') as f:
        w = csv.writer(f); w.writerow(cols)
        for k, v in counter.most_common(n):
            w.writerow([*(k if isinstance(k, tuple) else (k,)), v // 1000, *( [header_tus[k]] if name == 'headers.csv' else [])])
dump('headers.csv', headers, ['header', 'ms_inclusive', 'tus'])
dump('templates.csv', templates, ['kind', 'template', 'ms'])
dump('templates_full.csv', template_full, ['kind', 'instantiation', 'ms'])
dump('passes.csv', passes, ['event', 'pass', 'ms'])
(out / 'slowest').mkdir(exist_ok=True)
for t in tus[:12]:
    with open(t['trace'], 'rb') as src, gzip.open(out / 'slowest' / (t['tu'].replace('/', '__') + '.gz'), 'wb') as dst:
        shutil.copyfileobj(src, dst)
