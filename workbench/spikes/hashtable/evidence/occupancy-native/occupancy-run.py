#!/usr/bin/env python3
"""Matched complete-operation inactive-fingerprint experiment and offline report."""
import argparse
from collections import defaultdict
import csv
import hashlib
import io
import json
from pathlib import Path
import random
from statistics import median
import subprocess
import time

CASES = ['16_2_1', '40_0_0', '40_2_1', '40_8_2', '64_16_1']
SHAPES = [(0,128),(-1,128),(-2,128),(1,128),(0,96),(-1,96),(0,192),(-1,192),
          (0,64),(1,64),(-2,96),(1,96),(-2,192),(1,192),(-1,64),(-2,64)]
PHASES = ['construct','build','hit','miss','serial','update','erase',
          'aged_hit','aged_miss','reuse','reused_hit','release','lifecycle']
ENCODINGS = ['count','inactive']

def schedule(cases, arms, passes):
    order = [(c,e,a,l) for c in cases for e in ENCODINGS for a in arms for l in (0,1)]
    random.Random(811).shuffle(order)
    return [(s,*case) for s in range(passes) for case in (order if s%2==0 else reversed(order))]

def binary(case, encoding):
    return f'occupancy-{case}-{encoding}'

def run(a):
    a.output.mkdir(parents=True,exist_ok=True)
    paths = {binary(c,e):(a.binaries/binary(c,e)).resolve() for c in a.cases for e in ENCODINGS}
    receipt = dict(cases=a.cases,arms=a.arms,passes=a.passes,reps=a.reps,processes=[],
                   binary_sha256={n:hashlib.sha256(p.read_bytes()).hexdigest() for n,p in paths.items()})
    with (a.output/'values.csv').open('w') as out:
        writer = None
        for index,(s,c,e,arm,large) in enumerate(schedule(a.cases,a.arms,a.passes)):
            k,w,kind = map(int,c.split('_'))
            cmd = ['taskset','-c',a.cpu,str(paths[binary(c,e)]),str(kind),str(arm),'1',str(large),str(a.reps)]
            before = time.time()
            result = subprocess.run(cmd,capture_output=True,text=True)
            receipt['processes'].append(dict(argv=cmd,started=before,ended=time.time(),
                returncode=result.returncode,stderr=result.stderr))
            (a.output/'processes.json').write_text(json.dumps(receipt,indent=2)+'\n')
            if result.returncode:
                raise RuntimeError((cmd,result.stdout,result.stderr))
            rows = list(csv.DictReader(io.StringIO(result.stdout)))
            assert len(rows)==len(PHASES)*a.reps
            for row in rows:row.update(encoding=e,pass_index=s,process_order=index)
            if writer is None:
                writer=csv.DictWriter(out,fieldnames=list(rows[0]));writer.writeheader()
            else:assert writer.fieldnames==list(rows[0])
            writer.writerows(rows);out.flush()
            print(index+1,c,e,arm,large,flush=True)
    report(a.output,True)

def report(directory,verify):
    receipt=json.loads((directory/'processes.json').read_text())
    cases,arms,passes,reps=(receipt[f] for f in ('cases','arms','passes','reps'))
    assert set(cases)<=set(CASES) and len(set(cases))==len(cases)
    assert set(arms)<=set(range(18)) and 16 in arms and 17 in arms and len(set(arms))==len(arms)
    order=schedule(cases,arms,passes)
    rows=list(csv.DictReader((directory/'values.csv').open()))
    assert len(receipt['processes'])==len(order) and len(rows)==len(order)*reps*len(PHASES)
    groups=defaultdict(list);verified=set()
    for index,((s,c,e,arm,large),process) in enumerate(zip(order,receipt['processes'])):
        k,w,kind=map(int,c.split('_'));argv=process['argv']
        assert process['returncode']==0 and process['ended']>=process['started']
        if index:assert process['started']>=receipt['processes'][index-1]['ended']
        assert argv[:2]==['taskset','-c'] and list(map(int,argv[-5:]))==[kind,arm,1,large,reps]
        if verify and (c,e) not in verified:
            p=directory/binary(c,e)
            if not p.exists():p=Path(argv[3])
            assert hashlib.sha256(p.read_bytes()).hexdigest()==receipt['binary_sha256'][binary(c,e)]
            verified.add((c,e))
        ref=10 if large else 6;n=6<<ref;rounds=max(8,65536//n)
        shift,unit=SHAPES[arm] if arm<16 else (0,0);level=ref+shift
        # Eight payload bytes and a separate byte tag for the one-inline W8
        # multimap control; sets and maps use exactly their W payload bytes.
        def width(bits):return 1+(max(0,k-bits-8)+7)//8+(9 if kind==2 else w)
        def slots(bits,head):return min(32,(unit-head)//width(bits))
        for i,row in enumerate(rows[index*reps*13:(index+1)*reps*13]):
            phase=PHASES[i%13]
            if kind==0 and phase=='serial':phase='ordered'
            assert row['encoding']==e and row['phase']==phase
            assert tuple(int(row[f]) for f in ('K','value_bytes','kind','arm','fanout','reference_A','A','n','rep','pass_index','process_order'))==(
                k,w,kind,arm,1,ref,level,n,i//13,s,index)
            operations=1 if phase in ('construct','release') else n//2 if phase in ('erase','reuse') else n*rounds//2 if phase in ('aged_hit','aged_miss') else n*rounds if phase in ('hit','miss','serial','ordered','reused_hit') else n
            assert int(row['operations'])==operations and float(row['ns'])>=0
            assert tuple(int(row[f]) for f in ('B','bucket_bytes','primary_slots','secondary_slots'))==(
                (level-4,unit,slots(level,5),slots(level-4,9)) if arm<16 else (0,0,0,0))
            population=[int(row['build_'+t+'_rows']) for t in ('primary','secondary','overflow')]
            assert sum(population)==(n if arm<16 else 0)
            for tier,bits,header in (('primary',level,5),('secondary',level-4,9)):
                used=header+slots(bits,header)*width(bits)
                assert tuple(int(row[tier+'_'+f]) for f in ('used','padding'))==((used,unit-used) if arm<16 else (0,0))
            groups[c,e,ref,arm,phase].append(row)
    ns=lambda batch:median(float(r['ns'])/int(r['operations']) for r in batch)
    logical=lambda batch:median(int(r['requested'])+int(r['metadata']) for r in batch)
    comparisons=[]
    inventory=('requested','metadata','owned_capacity','primary_slots','secondary_slots','primary_used','secondary_used',
               'build_primary_rows','build_secondary_rows','build_overflow_rows')
    for (c,e,ref,arm,phase),batch in sorted(groups.items()):
        assert len(batch)==passes*reps
        other=groups[c,'inactive' if e=='count' else 'count',ref,arm,phase]
        for field in inventory:
            assert sorted(int(r[field]) for r in batch)==sorted(int(r[field]) for r in other),(c,arm,phase,field)
        ordinary=groups[c,e,ref,16,phase];exact=groups[c,e,ref,17,phase]
        count=groups[c,'count',ref,arm,phase]
        row=dict(case=c,encoding=e,reference_A=ref,arm=arm,phase=phase,ns_per_operation=ns(batch),
                 over_count=ns(batch)/ns(count),over_abseil=ns(batch)/ns(ordinary),over_exact=ns(batch)/ns(exact),
                 logical_bytes=logical(batch),logical_over_abseil=logical(batch)/logical(ordinary))
        for field in inventory[3:]:
            values={int(r[field]) for r in batch};assert len(values)==1;row[field]=values.pop()
        for s in range(passes):row[f'pass_{s}_ns']=ns([r for r in batch if int(r['pass_index'])==s])
        comparisons.append(row)
    summary=dict(rows=len(rows),processes=len(order),comparison=comparisons)
    (directory/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    with (directory/'summary.csv').open('w') as out:
        writer=csv.DictWriter(out,fieldnames=list(comparisons[0]));writer.writeheader();writer.writerows(comparisons)
    print(f'Validated {len(rows)} rows / {len(order)} processes; encoding inventories agree')
    for r in comparisons:
        if r['encoding']=='inactive' and r['reference_A']==10 and r['arm']<16 and r['phase'] in ('hit','serial','aged_hit','aged_miss','erase','lifecycle'):
            print(*(r[f] for f in ('case','arm','phase')),*(f'{r[f]:.3f}' for f in ('ns_per_operation','over_count','over_abseil')))

if __name__=='__main__':
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest='mode',required=True)
    run_p=sub.add_parser('run');run_p.add_argument('binaries',type=Path);run_p.add_argument('output',type=Path)
    run_p.add_argument('--cpu',required=True);run_p.add_argument('--reps',type=int,default=5);run_p.add_argument('--passes',type=int,default=2)
    run_p.add_argument('--cases',nargs='+',choices=CASES,default=CASES)
    run_p.add_argument('--arms',nargs='+',type=int,default=list(range(18)))
    report_p=sub.add_parser('report');report_p.add_argument('output',type=Path);report_p.add_argument('--verify-binaries',action='store_true')
    a=p.parse_args()
    if a.mode=='run':
        assert a.reps>0 and a.passes>0 and 16 in a.arms and 17 in a.arms
        run(a)
    else:report(a.output,a.verify_binaries)
