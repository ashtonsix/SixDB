#!/usr/bin/env python3
"""Counterbalanced same-input baseline/candidate/vendor primary-branch study."""
import argparse,csv,hashlib,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binaries',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cpu',required=True);p.add_argument('--arms',default='base,candidate,boost,abseil');p.add_argument('--hot-only',action='store_true');a=p.parse_args();chosen=a.arms.split(',')
a.output.mkdir(parents=True,exist_ok=True);records=[]
cases=[('hot32',32,0,11),('hot32',32,8,11),('hot40',40,0,12),('hot40',40,8,11),('cold40',40,0,22),('cold40',40,8,21),('cold50',50,0,22),('cold50',50,8,21)]
if a.hot_only: cases=[c for c in cases if c[0].startswith('hot')]
for trial in range(2):
 for si,seed in enumerate([713,991,1729]):
  for case,w,v,b in cases:
   cold=case.startswith('cold');n=14260633 if cold else 13926
   arms=chosen[:];rot=(trial+si)%len(arms);arms=arms[rot:]+arms[:rot]
   if (trial+si)%2: arms.reverse()
   for arm in arms:
    # One table population per process; all three selectivities and both direct
    # and prefetch paths are measured without rebuilding between them.
    impl=arm if arm in ['boost','abseil'] else 'ikea'
    binary=a.binaries/(f'{arm}-memory_ikea_{w}_{b}_{v}' if impl=='ikea' else f'base-memory_peers_{w}_{v}')
    stem=f'{case}-v{v}-s{seed}-t{trial}-{arm}'
    command=['taskset','-c',a.cpu,str(binary),'--impl',impl,'--n',str(n),'--reserve',str(n*2 if arm=='abseil' else n),'--seed',str(seed),'--requests','1000000','--repetitions','3','--writes','0','--read-prefetch','33' if cold else '0','--conditioning',str(256*1024**2 if cold else 0),'--pmu','1']
    start=time.time()
    with (a.output/(stem+'.csv')).open('w') as out,(a.output/(stem+'.jsonl')).open('w') as err:
     child=subprocess.run(command,stdout=out,stderr=err)
    r=dict(case=case,w=w,v=v,b=b if impl=='ikea' else 0,arm=arm,impl=impl,seed=seed,trial=trial,command=command,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),start=start,end=time.time(),exit=child.returncode,file=stem+'.csv',counters=stem+'.jsonl')
    records.append(r);(a.output/'runs.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(r),flush=True)
    assert child.returncode==0,r
    rows=[x for x in csv.DictReader((a.output/r['file']).open()) if x['phase']=='read'];assert len(rows)==(18 if cold and arm!='boost' else 9),r
    counters=[json.loads(x) for x in (a.output/r['counters']).read_text().splitlines()];assert len(counters)==len(rows),r
assert len(records)==len(cases)*6*len(chosen)
