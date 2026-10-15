#!/usr/bin/env python3
"""Complete hierarchy operations: scalar/explicit-SIMD packets and two baselines."""
import argparse
from collections import defaultdict
import csv
import hashlib
import io
import json
from pathlib import Path
import random
import shutil
from statistics import median
import subprocess
import time

def report(out):
    receipt=json.loads((out/'processes.json').read_text());rows=list(csv.DictReader((out/'values.csv').open()))
    for case,digest in receipt['binary_sha256'].items():
        assert hashlib.sha256((out/('hashtable_design_hierarchy_packet_'+case)).read_bytes()).hexdigest()==digest
    groups=defaultdict(list)
    phases=('construct','build','hit','miss','update','aged_hit','erase','reuse','release','lifecycle')
    assert len(rows)==len(receipt['processes'])*receipt['reps']*len(phases)
    for i,process in enumerate(receipt['processes']):
        assert process['returncode']==0 and not process['stderr']
        if i: assert process['start']>=receipt['processes'][i-1]['end']
        case,n,arm,width,pass_index=process['case'],process['n'],process['arm'],process['width'],process['pass_index']
        key,bytes_,kind=map(int,case.split('_')[:3])
        for j,row in enumerate(rows[i*receipt['reps']*10:(i+1)*receipt['reps']*10]):
            assert [int(row[x]) for x in ('K','W','kind')]==[key,bytes_,kind]
            assert row['case']==case and [int(row[x]) for x in ('n','arm','N','pass_index','rep')]==[n,arm,width,pass_index,j//10]
            assert row['phase']==phases[j%10] and int(row['operations'])>0 and float(row['ns'])>=0
            groups[case,str(n),str(arm),str(width),row['phase']].append(row)
    cost=lambda batch:median(float(r['ns'])/int(r['operations']) for r in batch)
    logical=lambda batch:median(int(r['requested'])+int(r['metadata']) for r in batch)
    summary=[]
    for (case,n,arm,width,phase),batch in sorted(groups.items()):
        assert len(batch)==2*receipt['reps']
        point=groups[case,n,arm,'1',phase]
        for field in ('operations','requested','metadata','primary','secondary','overflow'):
            assert sorted(int(r[field]) for r in batch)==sorted(int(r[field]) for r in point),(case,n,arm,width,phase,field)
        r=dict(case=case,n=n,arm=arm,N=width,phase=phase,ns_per_operation=cost(batch),over_scalar=cost(batch)/cost(point),
            over_abseil=cost(batch)/cost(groups[case,n,'1',width,phase]),over_exact=cost(batch)/cost(groups[case,n,'2',width,phase]),
            logical_bytes=logical(batch),logical_over_abseil=logical(batch)/logical(groups[case,n,'1',width,phase]))
        for p in (0,1):r[f'pass_{p}_ns']=cost([x for x in batch if int(x['pass_index'])==p])
        summary.append(r)
    with (out/'summary.csv').open('w') as f:
        w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
    indexed={(r['case'],r['n'],r['arm'],r['N'],r['phase']):r for r in summary}
    paired=[]
    for row in summary:
        if not row['case'].endswith('_compact'):continue
        key=(row['case'].removesuffix('_compact'),row['n'],row['arm'],row['N'],row['phase'])
        if key not in indexed:continue
        control=indexed[key]
        batch=groups[row['case'],row['n'],row['arm'],row['N'],row['phase']]
        for field in ('operations','requested','metadata','primary','secondary','overflow'):
            assert sorted(int(r[field]) for r in batch)==sorted(int(r[field]) for r in groups[key]),(key,field)
        pair=dict(row,compact_over_planes=row['ns_per_operation']/control['ns_per_operation'])
        for p in (0,1):pair[f'compact_over_planes_pass_{p}']=row[f'pass_{p}_ns']/control[f'pass_{p}_ns']
        paired.append(pair)
    if paired:
        with (out/'paired.csv').open('w') as f:
            w=csv.DictWriter(f,fieldnames=list(paired[0]));w.writeheader();w.writerows(paired)
    print('Validated',len(rows),'rows from',len(receipt['processes']),'processes; scalar/packet inventories agree')
    for r in summary:
        if r['arm']=='0' and r['N']!='1' and r['phase'] in ('hit','miss','update','lifecycle'):
            print(*(r[k] for k in ('case','n','N','phase')),*(f'{r[k]:.3f}' for k in ('ns_per_operation','over_scalar','over_abseil','logical_over_abseil')))

def run(a):
    out=a.output;out.mkdir(parents=True,exist_ok=True);assert not (out/'values.csv').exists()
    files={};receipt=dict(reps=a.reps,cases=a.cases,counts=a.counts,binary_sha256={},processes=[])
    for case in a.cases:
        name='hashtable_design_hierarchy_packet_'+case;target=out/name;shutil.copy2(a.binaries/name,target);files[case]=target.resolve()
        receipt['binary_sha256'][case]=hashlib.sha256(target.read_bytes()).hexdigest()
    order=[(c,n,arm,width) for c in a.cases for n in a.counts if int(c.split('_')[0])!=8 or n<=240 for arm in range(3) for width in (1,4,8)]
    random.Random(9771).shuffle(order);rows=[]
    for p,sequence in enumerate((order,list(reversed(order)))):
        for case,n,arm,width in sequence:
            command=['taskset','-c',a.cpu,str(files[case]),str(width),str(arm),str(n),str(a.reps)]
            start=time.time();r=subprocess.run(command,capture_output=True,text=True)
            receipt['processes'].append(dict(command=command,case=case,n=n,arm=arm,width=width,pass_index=p,start=start,end=time.time(),returncode=r.returncode,stderr=r.stderr))
            (out/'processes.json').write_text(json.dumps(receipt,indent=2)+'\n')
            assert r.returncode==0 and not r.stderr,(command,r.stdout,r.stderr)
            batch=list(csv.DictReader(io.StringIO(r.stdout)));assert len(batch)==a.reps*10
            for row in batch:row.update(case=case,pass_index=p)
            rows.extend(batch)
    with (out/'values.csv').open('w') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    report(out)

if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);sub=p.add_subparsers(dest='mode',required=True)
    r=sub.add_parser('run');r.add_argument('binaries',type=Path);r.add_argument('output',type=Path);r.add_argument('--cpu',required=True)
    r.add_argument('--cases',nargs='+',default=['40_2_1','40_2_2']);r.add_argument('--counts',nargs='+',type=int,default=[64,192,768]);r.add_argument('--reps',type=int,default=5)
    v=sub.add_parser('report');v.add_argument('output',type=Path)
    a=p.parse_args();run(a) if a.mode=='run' else report(a.output)
