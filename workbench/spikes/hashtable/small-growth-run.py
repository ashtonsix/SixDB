#!/usr/bin/env python3
"""Matched small-list lifetimes; ordinary Abseil, both process orders, exact bytes."""
import argparse
import csv
import hashlib
import importlib.util
import json
import subprocess
import time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('binaries', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--cpu', required=True)
p.add_argument('--keys', nargs='+', type=int, default=[8,16,24,32,40,48,56,64])
p.add_argument('--reps', type=int, default=7)
p.add_argument('--histories', type=int, default=64)
p.add_argument('--full-owner-control', action='store_true')
a = p.parse_args()
assert a.reps > 0 and 0 < a.histories <= 65536
a.output.mkdir(parents=True, exist_ok=False)
spec = importlib.util.spec_from_file_location('surface_report', Path(__file__).with_name('surface-report.py'))
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
context = dict(histories_per_timer=a.histories, repetitions=a.reps, processes=[], binary_sha256={})
summary = []
for order in (0,1):
    cases = [(key,grow,owner) for key in a.keys for grow in (0,1)
             for owner in (['selected','full'] if a.full_owner_control else ['selected'])]
    if order: cases.reverse()
    for key,grow,owner in cases:
        suffix = '_full' if owner == 'full' else ''
        name = f'small-batch-{key}-grow{grow}{suffix}-order{order}'
        exe = (a.binaries / f'hashtable_design_small_batch_{key}_grow{grow}{suffix}').resolve()
        context['binary_sha256'][exe.name] = hashlib.sha256(exe.read_bytes()).hexdigest()
        command = ['taskset','-c',a.cpu,str(exe),'bench',str(a.reps),str(a.histories)]
        start = time.time()
        with (a.output/f'{name}.csv').open('w') as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.PIPE, text=True)
        context['processes'].append(dict(key=key,grow=grow,owner_state=owner,order=order,command=command,
            start=start,end=time.time(),returncode=result.returncode,stderr=result.stderr))
        (a.output/'context.json').write_text(json.dumps(context,indent=2)+'\n')
        assert result.returncode == 0 and not result.stderr, context['processes'][-1]
        raw = list(csv.DictReader((a.output/f'{name}.csv').open()))
        assert {int(r['lifecycle_histories']) for r in raw} == {a.histories}
        report.WIDTHS = {key}
        rows = report.batch(a.output/f'{name}.csv')
        summary.extend(r | dict(grow=grow,owner_state=owner,order=order,lifecycle_histories=a.histories) for r in rows)
with (a.output/'summary.csv').open('w') as stream:
    writer = csv.DictWriter(stream,fieldnames=list(summary[0]))
    writer.writeheader(); writer.writerows(summary)
print(f'{len(context["processes"])} processes; {len(summary)} complete-history summaries; all oracles passed')
