#!/usr/bin/env python3
"""Verify and report complete capacity-family histories; model hints separately."""
import argparse,csv,hashlib,json,math,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('panel',type=Path);p.add_argument('--output',type=Path,required=True)
p.add_argument('--reps',type=int,default=3);p.add_argument('--sizes',nargs='+',type=int,default=[0,1])
p.add_argument('--loads',nargs='+',type=int,default=[50,100,125])
p.add_argument('--seeds',nargs='+',type=int,default=[41,127,901])
p.add_argument('--fanouts',nargs='+',type=int,default=[1,2,8]);a=p.parse_args()
receipts=json.loads((a.panel/'processes.json').read_text())
rows=list(csv.DictReader((a.panel/'operations.csv').open()))
cases=set(receipts['binary_sha256']);assert cases and rows and a.reps>0
phases=['construct','build','hit','miss','serial','update','erase','aged_hit','aged_miss','reuse','reused_hit','release','lifecycle']
expected=set()
for case in cases:
    k,w,kind=map(int,case.split('_'))
    for arm in range(10):
      for size in a.sizes:
       for load in a.loads:
        for seed in a.seeds:
         for fan in (a.fanouts if kind==2 else [1]):
          for rep in range(a.reps):expected.add((case,arm,size,load,seed,fan,rep))
assert len(receipts['processes'])==len(expected)
assert len(rows)==13*len(expected)
histories={};groups={}
fields=['case','arm','size','load','placement_seed','fanout','phase']
for r in rows:
    case=r['case'];k,w,kind=map(int,case.split('_'))
    arm,size,load,seed,fan,rep=(int(r[f]) for f in ['arm','size','load','placement_seed','fanout','process_rep'])
    identity=(case,arm,size,load,seed,fan,rep)
    assert identity in expected
    assert (int(r['K']),int(r['value_bytes']),int(r['kind']))==(k,w,kind)
    n=(6144 if size else 384)*load//100;assert int(r['n'])==n
    assert abs(float(r['ns_per_operation'])-float(r['ns'])/int(r['operations']))<1e-6
    expected_ops=1 if r['phase'] in ['construct','release'] else n
    if r['phase'] in ['hit','miss','serial','ordered','reused_hit']:expected_ops=max(8,65536//n)*n
    if r['phase']=='aged_hit':expected_ops=max(8,65536//n)*(n//2)
    if r['phase']=='aged_miss':expected_ops=max(8,65536//n)*((n+1)//2)
    if r['phase'] in ['erase','reuse']:expected_ops=(n+1)//2
    assert int(r['operations'])==expected_ops
    assert sum(int(r[f]) for f in ['build_primary_rows','build_secondary_rows','build_overflow_rows'])==(n if arm<8 else 0)
    if arm<8:
        reference=10 if size else 6
        family=(arm-2)%3 if arm>=2 else 0
        A=reference-(family if arm>=2 else 0);B=A-(2 if arm>=2 else 4)
        assert int(r['A'])==A and int(r['B'])==B
        stored=(16 if w<=16 else 8) if kind==2 else w
        for secondary in [False,True]:
            label='secondary' if secondary else 'primary'
            address=B if secondary else A
            header=9 if secondary else 5
            stride=1+(max(0,k-address-8)+7)//8+stored+(kind==2)
            slots=int(r[label+'_slots']);unit=int(r['secondary_bucket_bytes' if secondary else 'bucket_bytes'])
            used=header+slots*stride
            assert 1<=slots<=32 and used==int(r[label+'_used']) and unit-used==int(r[label+'_padding'])
            assert slots==32 or used+stride>unit
            if arm<2:assert unit==[128,192][arm]
            else:
                requested=([3,5,8] if secondary else [7,14,28])[family]
                quantum=8 if arm<5 else 64
                assert unit==(header+requested*stride+quantum-1)//quantum*quantum
    histories.setdefault(identity,[]).append(r)
    groups.setdefault(tuple(r[f] for f in fields),[]).append(r)
assert set(histories)==expected
for record,receipt in zip([rows[i:i+13] for i in range(0,len(rows),13)],receipts['processes']):
    assert receipt['returncode']==0 and not receipt['stderr']
    first=record[0];cmd=receipt['command'];kind=int(first['kind'])
    assert Path(cmd[3]).name=='hashtable_design_capacity_'+first['case']
    assert list(map(int,cmd[4:]))==[kind,int(first['arm']),int(first['fanout']),int(first['size']),1,int(first['n']),int(first['placement_seed'])]
    assert receipt['rep']==int(first['process_rep'])
for identity,batch in histories.items():
    kind=int(identity[0].split('_')[2])
    assert [r['phase'] for r in batch]==['ordered' if kind==0 and phase=='serial' else phase for phase in phases]
    by_phase={r['phase']:float(r['ns']) for r in batch}
    assert abs(sum(by_phase[p] for p in ['construct','build','update','erase','reuse','release'])-by_phase['lifecycle'])<.01
summary=[]
for key,batch in sorted(groups.items()):
    assert len(batch)==a.reps
    out=dict(zip(fields,key));out['samples']=len(batch)
    for field in ['n','A','B','bucket_bytes','secondary_bucket_bytes','primary_slots','secondary_slots']:
        assert len({r[field] for r in batch})==1;out[field]=int(batch[0][field])
    for field in ['ns_per_operation','requested','owned_capacity','metadata','build_primary_rows','build_secondary_rows','build_overflow_rows']:
        out[field]=statistics.median(float(r[field]) for r in batch)
    if int(out['arm'])>=8:out['owned_capacity']=-1 # baseline backing is unmeasured, not zero
    out['logical_bytes']=out['requested']+out['metadata']
    out['min_ns']=min(float(r['ns_per_operation']) for r in batch);out['max_ns']=max(float(r['ns_per_operation']) for r in batch)
    def lines(unit):
        return statistics.mean((offset+unit+63)//64 for offset in range(0,64,math.gcd(unit,64)))
    fraction=(out['build_secondary_rows']+out['build_overflow_rows'])/out['n']
    out['modeled_bucket_hint_bytes']=out['bucket_bytes']+fraction*out['secondary_bucket_bytes'] if int(out['arm'])<8 else -1
    out['modeled_bucket_hint_lines']=lines(out['bucket_bytes'])+fraction*lines(out['secondary_bucket_bytes']) if int(out['arm'])<8 else -1
    summary.append(out)
a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
(a.output/'inputs.json').write_text(json.dumps({name:hashlib.sha256((a.panel/name).read_bytes()).hexdigest() for name in ['operations.csv','processes.json']},indent=2)+'\n')
print(f'{len(histories)} complete histories; independent byte-fit, operation-count and coverage checks pass. Hint costs are modeled, not measured traffic.')
