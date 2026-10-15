#!/usr/bin/env python3
"""Matched external owners; full-operation oracles execute inside every process."""
import argparse,csv,hashlib,io,json,random,statistics,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binaries',type=Path);p.add_argument('output',type=Path)
p.add_argument('--cpu',default='0');p.add_argument('--reps',type=int,default=3)
p.add_argument('--widths',nargs='+',type=int,default=[2,8,32,64]);p.add_argument('--sizes',nargs='+',type=int,default=[1])
p.add_argument('--arms',nargs='+',type=int,default=list(range(10)));p.add_argument('--fanouts',nargs='+',type=int)
p.add_argument('--prefix',default='hashtable_design_vectors')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
assert not (a.output/'processes.json').exists()
phases=['construct','build','hit','miss','serial','update','erase','aged_hit','aged_miss','reuse','reused_hit','release','lifecycle']
records=[];rows=[];identities={}
for w in a.widths:
    exe=a.binaries/f'{a.prefix}_{w}';identities[str(w)]=hashlib.sha256(exe.read_bytes()).hexdigest()
schedule=[]
for w in a.widths:
    cap=(16 if w<=8 else 64)//w
    fans=a.fanouts if a.fanouts is not None else sorted(set([0,1,cap,cap+1,2*cap,2*cap+1,64]))
    schedule += [(w,arm,fan,size) for arm in a.arms for fan in fans for size in a.sizes]
random.Random(3181).shuffle(schedule)
with (a.output/'operations.csv').open('w') as f:
    writer=None
    for rep in range(a.reps):
        order=schedule[rep%len(schedule):]+schedule[:rep%len(schedule)]
        if rep%2:order=list(reversed(order))
        for w,arm,fan,size in order:
            cmd=['taskset','-c',a.cpu,str((a.binaries/f'{a.prefix}_{w}').resolve()),'2',str(arm),str(fan),str(size),'1']
            start=time.time();r=subprocess.run(cmd,text=True,capture_output=True)
            records.append(dict(rep=rep,width=w,arm=arm,fanout=fan,size=size,command=cmd,start=start,end=time.time(),returncode=r.returncode,stderr=r.stderr))
            (a.output/'processes.json').write_text(json.dumps(dict(binary_sha256=identities,processes=records),indent=2)+'\n')
            assert r.returncode==0 and not r.stderr,(cmd,r.stdout,r.stderr)
            batch=list(csv.DictReader(io.StringIO(r.stdout)));assert [x['phase'] for x in batch]==phases
            for x in batch:
                assert int(x['arm'])==arm and int(x['value_bytes'])==w and int(x['fanout'])==fan
                assert int(x['kind'])==2 and int(x['n'])==6*(1<<(10 if size else 6))
                x['process_rep']=rep;x['size']=size
                x['ns_per_operation']=float(x['ns'])/int(x['operations']);rows.append(x)
            if writer is None:writer=csv.DictWriter(f,fieldnames=list(batch[0]));writer.writeheader()
            writer.writerows(batch);f.flush()
groups={}
for x in rows:groups.setdefault(tuple(x[k] for k in ('value_bytes','size','arm','fanout','phase')),[]).append(x)
summary=[]
for key,values in sorted(groups.items()):
    assert len(values)==a.reps
    x=dict(zip(('value_bytes','size','arm','fanout','phase'),key))
    for field in ('ns_per_operation','requested','metadata','owned_capacity'):
        x[field]=statistics.median(float(v[field]) for v in values)
    x['logical_bytes']=x['requested']+x['metadata'];x['samples']=len(values)
    if 'reserve_block' in values[0]:
        x['reserve_block']=int(values[0]['reserve_block'])
        assert {int(v['reserve_block']) for v in values}=={x['reserve_block']}
        for field in ('pool_allocations','pool_growths','pool_relocations','pool_mapped_bytes',
                      'pool_reserved_bytes','pool_metadata_bytes','pool_idle_bytes'):
            x[field]=statistics.median(int(v[field]) for v in values)
    x['min_ns']=min(v['ns_per_operation'] for v in values);x['max_ns']=max(v['ns_per_operation'] for v in values)
    summary.append(x)
with (a.output/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
print(f'{len(records)} processes; {len(rows)} checked phase rows; all operations/oracles passed')
