#!/usr/bin/env python3
"""Coupled wide-MM budgets, bucket geometry and storage beside two Abseil controls."""
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

# budget, byte unit, links/block, storage, bucket-count shift from reference A.
SHAPES = [(8,128,16,'links',0), (16,192,16,'links',0), (32,192,16,'links',1),
          (64,96,16,'links',3), (64,128,16,'links',3), (64,160,16,'links',2),
          (64,192,16,'links',2), (64,256,16,'links',2), (64,320,16,'links',1),
          (64,320,256,'links',1), (64,320,0,'buffer',1)]


def phases(extended):
    return ['construct','build','hit','miss','serial','update','erase'] + (
        ['aged_hit','aged_miss','reuse','reused_hit'] if extended else []) + ['release','lifecycle']


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def write_csv(p, rows):
    with p.open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)


def report(out):
    receipt = json.loads((out/'processes.json').read_text())
    rows = list(csv.DictReader((out/'values.csv').open()))
    phase_names = phases(receipt['extended'])
    schedule = [(c,a,f,s) for c in receipt['cases'] for a in receipt.get('arms',range(13))
                for f in receipt['fanouts'] for s in receipt['sizes']]
    random.Random(5197).shuffle(schedule)
    expected = [(p,*case) for p, sequence in enumerate((schedule,list(reversed(schedule)))) for case in sequence]
    assert len(receipt['processes']) == len(expected)
    assert len(rows) == len(expected)*receipt['reps']*len(phase_names)
    for name,h in receipt['binary_sha256'].items(): assert sha(out/name)==h
    groups = defaultdict(list)
    for order, ((p,c,arm,fan,large), proc) in enumerate(zip(expected,receipt['processes'])):
        assert proc['returncode']==0 and not proc['stderr']
        assert [proc[k] for k in ('pass_index','case','arm','fanout','large')]==[p,c,arm,fan,large]
        assert proc['end']>=proc['start']
        if order: assert proc['start']>=receipt['processes'][order-1]['end']
        assert list(map(int,proc['command'][-5:]))==[2,arm,fan,large,receipt['reps']]
        K,W=map(int,c.split('_')); ref=10 if large else 6; n=6<<ref
        budget,unit,slots,storage,shift=SHAPES[arm if arm<11 else 0]
        begin=order*receipt['reps']*len(phase_names)
        for j,r in enumerate(rows[begin:begin+receipt['reps']*len(phase_names)]):
            assert [int(r[k]) for k in ('pass_index','rep','K','value_bytes','kind','arm','fanout','A','n','reference_A')]==[p,j//len(phase_names),K,W,2,arm,fan,ref+shift,n,ref]
            assert r['case']==c and r['phase']==phase_names[j%len(phase_names)]
            phase=r['phase']; rounds=max(8,65536//n)
            ops=1 if phase in ('construct','release') else n//2 if phase in ('erase','reuse') else n*rounds//2 if phase in ('aged_hit','aged_miss') else n*rounds if phase in ('hit','miss','serial','reused_hit') else n
            assert int(r['operations'])==ops and float(r['ns'])>=0
            assert int(r['inline_values'])==(budget//W if arm<11 else 0)
            assert int(r['abseil_inline_capacity'])==(1 if arm>=11 else 0)
            assert sum(int(r[k]) for k in ('build_primary_rows','build_secondary_rows','build_overflow_rows'))==(n if arm<11 else 0)
            if arm<11:
                assert int(r['B'])==ref+shift-4 and int(r['bucket_bytes'])==unit
                for tier,A,header in (('primary',ref+shift,5),('secondary',ref+shift-4,9)):
                    suffix=(max(K-A-8,0)+7)//8
                    entry=1+suffix+budget+1
                    fitting=min(32,(unit-header)//entry)
                    assert int(r[tier+'_slots'])==fitting
                    assert int(r[tier+'_used'])==header+entry*fitting
                    assert int(r[tier+'_padding'])==unit-header-entry*fitting
            groups[c,large,fan,arm,phase].append(r)
    def ns(batch,p=None):
        return median(float(r['ns'])/int(r['operations']) for r in batch if p is None or int(r['pass_index'])==p)
    def memory(batch): return median(int(r['requested'])+int(r['metadata']) for r in batch)
    result=[]
    for (case,large,fan,arm,phase),batch in sorted(groups.items()):
        assert len(batch)==2*receipt['reps']
        base=groups[case,large,fan,11,phase]; exact=groups[case,large,fan,12,phase]
        budget,unit,slots,storage,shift=SHAPES[arm if arm<11 else 0]
        row=dict(case=case,large=large,fanout=fan,arm=arm,phase=phase,ns_per_operation=ns(batch),
                 over_abseil=ns(batch)/ns(base),over_exact=ns(batch)/ns(exact),
                 over_external8=ns(batch)/ns(groups[case,large,fan,0,phase]),
                 logical_bytes=memory(batch),logical_over_abseil=memory(batch)/memory(base),
                 inline_budget=budget if arm<11 else 0,bucket_bytes=unit if arm<11 else 0,
                 block_slots=slots if arm<11 else 0,storage=storage if arm<11 else 'abseil',
                 **{k:int(batch[0][k]) for k in ('A','B','n','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows')})
        for p in (0,1):
            row[f'pass_{p}_ns']=ns(batch,p); row[f'pass_{p}_over_abseil']=ns(batch,p)/ns(base,p)
        result.append(row)
    write_csv(out/'summary.csv',result)
    print(f'Validated {len(rows)} rows, {len(expected)} processes; all recipes match independent byte fits')
    for r in result:
        if r['arm']<11 and r['phase'] in ('hit','serial','update','lifecycle'):
            print(*(r[k] for k in ('case','large','fanout','arm','phase')),
                  *(f'{r[k]:.3f}' for k in ('ns_per_operation','over_abseil','logical_over_abseil')))


def run(a):
    out=a.output; out.mkdir(parents=True,exist_ok=True)
    assert not (out/'processes.json').exists()
    receipt=dict(cases=a.cases,arms=a.arms,fanouts=a.fanouts,sizes=a.sizes,reps=a.reps,extended=a.extended,binary_sha256={},processes=[])
    binaries={}
    for case in a.cases:
        name='hashtable_design_wide_bag_'+case; p=out/name
        shutil.copy2(a.binaries/name,p); binaries[case]=p.resolve(); receipt['binary_sha256'][name]=sha(p)
    schedule=[(c,arm,f,s) for c in a.cases for arm in a.arms for f in a.fanouts for s in a.sizes]
    random.Random(5197).shuffle(schedule)
    with (out/'values.csv').open('w') as stream:
        writer=None
        for pass_index,sequence in enumerate((schedule,list(reversed(schedule)))):
            for case,arm,fan,large in sequence:
                command=['taskset','-c',a.cpu,str(binaries[case]),'2',str(arm),str(fan),str(large),str(a.reps)]
                start=time.time(); r=subprocess.run(command,capture_output=True,text=True)
                receipt['processes'].append(dict(case=case,arm=arm,fanout=fan,large=large,pass_index=pass_index,
                                                 command=command,start=start,end=time.time(),returncode=r.returncode,stderr=r.stderr))
                (out/'processes.json').write_text(json.dumps(receipt,indent=2)+'\n')
                assert r.returncode==0 and not r.stderr,(command,r.stdout,r.stderr)
                rows=list(csv.DictReader(io.StringIO(r.stdout))); assert len(rows)==a.reps*len(phases(a.extended))
                for row in rows: row.update(case=case,pass_index=pass_index)
                if writer is None: writer=csv.DictWriter(stream,fieldnames=list(rows[0])); writer.writeheader()
                writer.writerows(rows); stream.flush()
    report(out)


def pair(a):
    """Interleave exact old/new ELFs, reversing both case and variant order."""
    a.output.mkdir(parents=True,exist_ok=False)
    receipts={}; binaries={}; streams={}; writers={}
    for variant,directory,suffix in [('control',a.control,''),('changed',a.binaries,a.suffix)]:
        out=a.output/variant;out.mkdir()
        receipts[variant]=dict(cases=a.cases,arms=a.arms,fanouts=a.fanouts,sizes=a.sizes,
            reps=a.reps,extended=a.extended,binary_sha256={},processes=[])
        for case in a.cases:
            name='hashtable_design_wide_bag_'+case;p=out/name
            shutil.copy2(directory/(name+suffix),p);binaries[variant,case]=p.resolve()
            receipts[variant]['binary_sha256'][name]=sha(p)
        streams[variant]=(out/'values.csv').open('w')
    schedule=[(c,arm,f,s) for c in a.cases for arm in a.arms for f in a.fanouts for s in a.sizes]
    random.Random(5197).shuffle(schedule)
    order=[]
    try:
        for pass_index,sequence in enumerate((schedule,list(reversed(schedule)))):
            for i,(case,arm,fan,large) in enumerate(sequence):
                original=i if pass_index==0 else len(schedule)-1-i
                variants=['control','changed'] if (original+pass_index)%2==0 else ['changed','control']
                for variant in variants:
                    command=['taskset','-c',a.cpu,str(binaries[variant,case]),'2',str(arm),str(fan),str(large),str(a.reps)]
                    start=time.time();r=subprocess.run(command,capture_output=True,text=True)
                    proc=dict(case=case,arm=arm,fanout=fan,large=large,pass_index=pass_index,
                        command=command,start=start,end=time.time(),returncode=r.returncode,stderr=r.stderr)
                    receipts[variant]['processes'].append(proc)
                    (a.output/variant/'processes.json').write_text(json.dumps(receipts[variant],indent=2)+'\n')
                    order.append(dict(variant=variant,**proc))
                    (a.output/'order.json').write_text(json.dumps(order,indent=2)+'\n')
                    assert r.returncode==0 and not r.stderr,(command,r.stdout,r.stderr)
                    rows=list(csv.DictReader(io.StringIO(r.stdout)));assert len(rows)==a.reps*len(phases(a.extended))
                    for row in rows:row.update(case=case,pass_index=pass_index)
                    if variant not in writers:
                        writers[variant]=csv.DictWriter(streams[variant],fieldnames=list(rows[0]));writers[variant].writeheader()
                    writers[variant].writerows(rows);streams[variant].flush()
    finally:
        for stream in streams.values():stream.close()
    for variant in receipts:report(a.output/variant)
    pair_report(a.output)


def pair_report(out):
    raw={v:list(csv.DictReader((out/v/'values.csv').open())) for v in ('control','changed')}
    assert len(raw['control'])==len(raw['changed'])
    for before,after in zip(raw['control'],raw['changed']):
        assert {k:v for k,v in before.items() if k!='ns'}=={k:v for k,v in after.items() if k!='ns'},'changed physical/logical inventory'
    order=json.loads((out/'order.json').read_text())
    records={v:json.loads((out/v/'processes.json').read_text())['processes'] for v in raw}
    for v in raw:assert [{k:x for k,x in p.items() if k!='variant'} for p in order if p['variant']==v]==records[v]
    assert len(order)==len(records['control'])*2
    for i,p in enumerate(order):
        if i:assert p['start']>=order[i-1]['end']
    first_variants={}
    for before,after in zip(order[::2],order[1::2]):
        assert before['variant']!=after['variant']
        assert all(before[k]==after[k] for k in ('case','arm','fanout','large','pass_index'))
        case=tuple(before[k] for k in ('case','arm','fanout','large'))
        if before['pass_index']==0:first_variants[case]=before['variant']
        else:assert before['variant']!=first_variants[case]
    key=lambda r:tuple(r[k] for k in ('case','large','fanout','arm','phase'))
    summaries={v:{key(r):r for r in csv.DictReader((out/v/'summary.csv').open())} for v in raw}
    result=[]
    for k,before in summaries['control'].items():
        after=summaries['changed'][k]
        row={field:before[field] for field in ('case','large','fanout','arm','phase')}
        row.update(control_ns=before['ns_per_operation'],changed_ns=after['ns_per_operation'],
            changed_over_control=float(after['ns_per_operation'])/float(before['ns_per_operation']),
            control_over_abseil=before['over_abseil'],changed_over_abseil=after['over_abseil'])
        for p in (0,1):row[f'pass_{p}_changed_over_control']=float(after[f'pass_{p}_ns'])/float(before[f'pass_{p}_ns'])
        result.append(row)
    write_csv(out/'comparison.csv',result)
    print(f'Paired {len(order)} sequential processes; every physical/logical inventory identical')


if __name__=='__main__':
    parser=argparse.ArgumentParser(__doc__); sub=parser.add_subparsers(dest='mode',required=True)
    for mode in ('run','pair'):
        r=sub.add_parser(mode); r.add_argument('binaries',type=Path); r.add_argument('output',type=Path)
        r.add_argument('--cpu',required=True); r.add_argument('--cases',nargs='+',default=['16_32','16_64','40_32','40_64','64_32','64_64'])
        r.add_argument('--fanouts',nargs='+',type=int,default=[0,1,2,3,8,32]); r.add_argument('--sizes',nargs='+',type=int,default=[0,1])
        r.add_argument('--arms',nargs='+',type=int,default=list(range(13)))
        r.add_argument('--reps',type=int,default=5); r.add_argument('--extended',action='store_true')
        if mode=='pair':r.add_argument('--control',type=Path,required=True);r.add_argument('--suffix',default='_zero')
    v=sub.add_parser('report'); v.add_argument('output',type=Path)
    v=sub.add_parser('pair-report'); v.add_argument('output',type=Path)
    a=parser.parse_args()
    if a.mode in ('run','pair'):
        assert {0,11,12}<=set(a.arms)<=set(range(13)) and len(a.arms)==len(set(a.arms))
        run(a) if a.mode=='run' else pair(a)
    elif a.mode=='report':report(a.output)
    else:
        for variant in ('control','changed'):report(a.output/variant)
        pair_report(a.output)
