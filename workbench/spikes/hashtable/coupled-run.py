#!/usr/bin/env python3
"""Full histories for asymmetric primary/secondary geometry controls."""
import argparse,csv,hashlib,io,json,random,statistics,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binaries',type=Path);p.add_argument('output',type=Path)
p.add_argument('--cases',nargs='+',default=['64_16_1']);p.add_argument('--cpu',default='0')
p.add_argument('--reps',type=int,default=3);p.add_argument('--sizes',nargs='+',type=int,default=[0,1])
p.add_argument('--capacity-families',action='store_true')
p.add_argument('--seeds',nargs='+',type=int,default=[41,127,901])
p.add_argument('--loads',nargs='+',type=int,default=[50,100,125])
p.add_argument('--fanouts',nargs='+',type=int,default=[1,2,8]);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);assert not (a.output/'processes.json').exists()
assert a.reps>0 and set(a.sizes)<={0,1}
assert a.seeds and all(0<=s<2**64-32 for s in a.seeds) and len(set(a.seeds))==len(a.seeds)
assert a.loads and all(0<l<=200 for l in a.loads) and len(set(a.loads))==len(a.loads)
assert a.fanouts and set(a.fanouts)<={1,2,3,8}
prefix='hashtable_design_capacity_' if a.capacity_families else 'hashtable_design_coupled_'
phases=['construct','build','hit','miss','serial','update','erase','aged_hit','aged_miss','reuse','reused_hit','release','lifecycle']
records=[];rows=[];identities={};schedule=[]
for case in a.cases:
    k,w,kind=map(int,case.split('_'));assert kind in (0,1,2) and ((kind==0)==(w==0))
    path=a.binaries/f'{prefix}{case}'
    identities[case]=hashlib.sha256(path.read_bytes()).hexdigest()
    schedule += [(case,arm,fan,size,load,seed) for arm in range(10 if a.capacity_families else 12)
                 for fan in ((a.fanouts if a.capacity_families else [1,2,3,8]) if kind==2 else [1]) for size in a.sizes
                 for load in (a.loads if a.capacity_families else [100]) for seed in (a.seeds if a.capacity_families else [41])]
random.Random(771).shuffle(schedule)
with (a.output/'operations.csv').open('w') as f:
    writer=None
    for rep in range(a.reps):
        order=schedule if rep%2==0 else list(reversed(schedule))
        for case,arm,fan,size,load,seed in order:
            k,w,kind=map(int,case.split('_'));exe=(a.binaries/f'{prefix}{case}').resolve()
            n=(6144 if size else 384)*load//100
            cmd=['taskset','-c',a.cpu,str(exe),str(kind),str(arm),str(fan),str(size),'1']
            if a.capacity_families:cmd += [str(n),str(seed)]
            start=time.time();r=subprocess.run(cmd,text=True,capture_output=True)
            records.append(dict(command=cmd,start=start,end=time.time(),rep=rep,returncode=r.returncode,stderr=r.stderr))
            if len(records)%32==0 or r.returncode or r.stderr:
                (a.output/'processes.json').write_text(json.dumps(dict(binary_sha256=identities,processes=records),indent=2)+'\n')
            assert r.returncode==0 and not r.stderr,(cmd,r.stdout,r.stderr)
            batch=list(csv.DictReader(io.StringIO(r.stdout)))
            assert [x['phase'] for x in batch]==['ordered' if kind==0 and p=='serial' else p for p in phases]
            for x in batch:
                assert int(x['K'])==k and int(x['value_bytes'])==w and int(x['kind'])==kind and int(x['arm'])==arm
                assert int(x['n'])==n
                if a.capacity_families:assert int(x['placement_seed'])==seed
                x.update(case=case,process_rep=rep,size=size,load=load,placement_seed=seed,
                         ns_per_operation=float(x['ns'])/int(x['operations']));rows.append(x)
            if writer is None:writer=csv.DictWriter(f,fieldnames=list(batch[0]));writer.writeheader()
            writer.writerows(batch);f.flush()
(a.output/'processes.json').write_text(json.dumps(dict(binary_sha256=identities,processes=records),indent=2)+'\n')
groups={}
group_fields=('case','arm','size','load','placement_seed','fanout','phase')
for r in rows:groups.setdefault(tuple(r[x] for x in group_fields),[]).append(r)
summary=[]
for key,batch in sorted(groups.items()):
    assert len(batch)==a.reps
    x=dict(zip(group_fields,key))
    for field in ('ns_per_operation','requested','owned_capacity','metadata','build_primary_rows','build_secondary_rows','build_overflow_rows'):
        x[field]=statistics.median(float(r[field]) for r in batch)
    for field in ('A','B','bucket_bytes','secondary_bucket_bytes','primary_slots','secondary_slots'):
        assert len({r[field] for r in batch})==1;x[field]=int(batch[0][field])
    x['logical_bytes']=x['requested']+x['metadata'];x['samples']=len(batch)
    x['min_ns']=min(r['ns_per_operation'] for r in batch);x['max_ns']=max(r['ns_per_operation'] for r in batch);summary.append(x)
with (a.output/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
print(f'{len(records)} complete histories, {len(rows)} phase rows; byte and mutation oracles passed')
