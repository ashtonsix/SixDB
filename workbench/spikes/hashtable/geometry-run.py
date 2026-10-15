#!/usr/bin/env python3
"""Fresh-process complete operations with fixed histories across geometries."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import random
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('binaries', type=Path, help='directory containing geometry-K-W')
p.add_argument('output', type=Path)
p.add_argument('--cpu', required=True)
p.add_argument('--keys', type=int, nargs='+', default=[16,24,32,40,48,56,64])
p.add_argument('--widths', type=int, nargs='+', default=[0,2,8,16])
p.add_argument('--passes', type=int, default=2)
p.add_argument('--reps', type=int, default=5)
mode=p.add_mutually_exclusive_group()
mode.add_argument('--selector-variants', action='store_true', help='interleave all four composed/direct and 16/32-byte execution controls')
mode.add_argument('--mask-variants', action='store_true', help='interleave ordinary/split/paired/AND hash mask source controls')
a = p.parse_args()
assert a.reps > 0 and a.passes > 0
assert len(set(a.keys)) == len(a.keys) and set(a.keys) <= {16,24,32,40,48,56,64}
assert len(set(a.widths)) == len(a.widths) and set(a.widths) <= {0,2,8,16}
a.output.mkdir(parents=True, exist_ok=True)
variants=['composed16','direct16','composed32','direct32'] if a.selector_variants else ['ordinary','split','paired','and'] if a.mask_variants else ['geometry']
family='selector' if a.selector_variants else 'mask' if a.mask_variants else None
def identity(v,k,w):return f'{k}-{w}' if v=='geometry' else f'{v}-{k}-{w}'
def name(v,k,w):return f'geometry-{k}-{w}' if v=='geometry' else f'{family}-{v}-{k}-{w}'
binaries = {(v,k,w): (a.binaries/name(v,k,w)).resolve() for v in variants for k in a.keys for w in a.widths}
receipt = dict(keys=a.keys, widths=a.widths, passes=a.passes, reps=a.reps, processes=[],
               binary_sha256={identity(v,k,w):hashlib.sha256(b.read_bytes()).hexdigest() for (v,k,w),b in binaries.items()})
if family:
    receipt['variants']=variants
    if family=='mask':receipt['variant_field']='mask_variant'
schedule = [(v,k,w,arm,large) for v in variants for k in a.keys for w in a.widths for arm in range(18) for large in (0,1)]
random.Random(991).shuffle(schedule)
full = [(s,*case) for s in range(a.passes) for case in (schedule if s%2==0 else reversed(schedule))]
with (a.output/'values.csv').open('w') as stream:
    writer = None
    for index,(s,v,k,w,arm,large) in enumerate(full):
        cmd = ['taskset','-c',a.cpu,str(binaries[v,k,w]),str(int(w!=0)),str(arm),'1',str(large),str(a.reps)]
        started = time.time()
        result = subprocess.run(cmd, capture_output=True, text=True)
        receipt['processes'].append(dict(argv=cmd, started_unix=started, ended_unix=time.time(),
                                         returncode=result.returncode, stderr=result.stderr))
        (a.output/'processes.json').write_text(json.dumps(receipt,indent=2)+'\n')
        if result.returncode:
            raise RuntimeError((cmd,result.stderr,result.stdout))
        rows = list(csv.DictReader(io.StringIO(result.stdout)))
        assert len(rows) == 9*a.reps
        for row in rows:row.update(pass_index=s,process_order=index)
        if family:
            for row in rows:row[family+'_variant']=v
        if writer is None:
            writer = csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader()
        else:assert writer.fieldnames == list(rows[0])
        writer.writerows(rows);stream.flush()
        print(f'{index+1}/{len(full)} {v} K{k} W{w} arm{arm} large{large}',flush=True)
