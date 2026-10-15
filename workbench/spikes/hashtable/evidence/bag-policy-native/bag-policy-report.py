#!/usr/bin/env python3
"""Validate the complete coupled-policy panel and retain every comparison."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path
import random
from statistics import median

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--verify-binaries', action='store_true')
a = p.parse_args()
receipt = json.loads((a.directory/'processes.json').read_text())
assert receipt['bag_policies']
widths, passes, reps = receipt['widths'], receipt['passes'], receipt['reps']
fans = {2: [0,1,4,5,8,9,12,13,16,17,32], 8: [0,1,2,3,4,5,8,32], 16: [0,1,2,3,8,32]}
shapes = [(8,128,16,'links'), (16,192,16,'links'), (24,256,16,'links'),
          (32,256,16,'links'), (32,320,16,'links'), (16,192,64,'links'),
          (16,192,256,'links'), (16,192,0,'buffer')]
phases = ['construct','build','hit','miss','serial','update','erase','release','lifecycle']
schedule = [(w,2,arm,fan,large) for w in widths for arm in range(10)
            for fan in fans[w] for large in (0,1)]
random.Random(912).shuffle(schedule)
full = [(s,*case) for s in range(passes) for case in (schedule if s%2==0 else reversed(schedule))]
rows = list(csv.DictReader((a.directory/'values.csv').open()))
assert len(receipt['processes']) == len(full)
assert len(rows) == len(full)*reps*len(phases)
groups = defaultdict(list)
verified = set()
for order, ((s,w,kind,arm,fan,large), process) in enumerate(zip(full,receipt['processes'])):
    assert process['returncode'] == 0 and process['ended_unix'] >= process['started_unix']
    if order:assert process['started_unix'] >= receipt['processes'][order-1]['ended_unix']
    argv = process['argv']
    assert argv[:2] == ['taskset','-c'] and list(map(int,argv[-5:])) == [kind,arm,fan,large,reps]
    if a.verify_binaries and w not in verified:
        assert hashlib.sha256(Path(argv[3]).read_bytes()).hexdigest() == receipt['binary_sha256'][str(w)]
        verified.add(w)
    chunk = rows[order*reps*9:(order+1)*reps*9]
    for i,r in enumerate(chunk):
        n = 6 << (10 if large else 6)
        assert tuple(int(r[f]) for f in ('pass_index','process_order','rep','value_bytes','kind','arm','fanout','A','n','K')) == (
            s,order,i//9,w,kind,arm,fan,10 if large else 6,n,40)
        assert r['phase'] == phases[i%9]
        operations = (1 if r['phase'] in ('construct','release') else n//2 if r['phase']=='erase'
                      else n*max(8,65536//n) if r['phase'] in ('hit','miss','serial') else n)
        assert int(r['operations']) == operations
        assert int(r['inline_values']) == (shapes[arm][0]//w if arm<8 else 0)
        assert int(r['abseil_inline_capacity']) == ({2:8,8:2,16:1}[w] if arm>=8 else 0)
        assert sum(int(r[f]) for f in ('build_primary_rows','build_secondary_rows','build_overflow_rows')) == (n if arm<8 else 0)
        groups[(w,int(r['A']),fan,arm,r['phase'])].append(r)

def ns(batch):return median(float(r['ns'])/int(r['operations']) for r in batch)
def logical(batch):return median(int(r['requested'])+int(r['metadata']) for r in batch)
comparison = []
for key,batch in sorted(groups.items()):
    w,level,fan,arm,phase = key
    baseline = groups[w,level,fan,8,phase]
    exact = groups[w,level,fan,9,phase]
    control = groups[w,level,fan,0,phase]
    assert len(batch) == reps*passes
    entry = dict(value_bytes=w,A=level,fanout=fan,arm=arm,phase=phase,
                 ns_per_operation=ns(batch),over_abseil=ns(batch)/ns(baseline),
                 over_exact_abseil=ns(batch)/ns(exact),over_control=ns(batch)/ns(control),
                 logical_bytes=logical(batch),logical_over_abseil=logical(batch)/logical(baseline),
                 metadata_bytes=median(int(r['metadata']) for r in batch),
                 owned_capacity_min=min(int(r['owned_capacity']) for r in batch),
                 owned_capacity_max=max(int(r['owned_capacity']) for r in batch),
                 build_primary_rows=int(batch[0]['build_primary_rows']),
                 build_secondary_rows=int(batch[0]['build_secondary_rows']),
                 build_overflow_rows=int(batch[0]['build_overflow_rows']),
                 inline_values=int(batch[0]['inline_values']))
    if arm<8:
        entry.update(zip(('inline_budget','bucket_bytes','links_per_block','storage'),shapes[arm]))
    else:entry.update(inline_budget=None,bucket_bytes=None,links_per_block=None,storage='abseil' if arm==8 else 'exact_abseil')
    for s in range(passes):entry[f'pass_{s}_ns'] = ns([r for r in batch if int(r['pass_index'])==s])
    comparison.append(entry)
a.output.write_text(json.dumps(dict(rows=len(rows),processes=len(full),comparison=comparison),indent=2)+'\n')
with a.output.with_suffix('.csv').open('w') as out:
    writer=csv.DictWriter(out,fieldnames=list(comparison[0]));writer.writeheader();writer.writerows(comparison)
print(f'Validated {len(rows)} rows, {len(full)} fresh processes and every complete-operation case')
print('Mixed fanout, A10: ratios >1 are slower/larger than ordinary Abseil')
for r in comparison:
    if r['A']==10 and r['fanout']==0 and r['phase'] in ('build','hit','serial','update','lifecycle'):
        print('W/arm/phase',r['value_bytes'],r['arm'],r['phase'],'ns/Abseil/logical',
              *(f'{r[f]:.3f}' for f in ('ns_per_operation','over_abseil','logical_over_abseil')))
