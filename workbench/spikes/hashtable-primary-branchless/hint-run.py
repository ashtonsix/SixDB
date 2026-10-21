#!/usr/bin/env python3
"""Predictable-priority hint/selector comparison with actual vendor controls."""
import argparse,csv,hashlib,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binaries',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cpu',required=True);p.add_argument('--grid',choices=['pilot','full'],default='pilot');a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);records=[]
cases=[('hot32',32,0,11),('hot32',32,8,11)]
if a.grid=='full':cases += [('hot40',40,0,12),('hot40',40,8,11),('cold40',40,0,22),('cold40',40,8,21),('cold50',50,0,22),('cold50',50,8,21)]
for trial in range(2):
 for si,seed in enumerate([713,991,1729]):
  for case,w,v,b in cases:
   cold=case.startswith('cold');n=14260633 if cold else 13926
   arms=['original','predictable','unpredictable','adaptive','boost','abseil'];rot=(trial+si)%len(arms);arms=arms[rot:]+arms[:rot]
   if (trial+si)%2:arms.reverse()
   plans=[(101,0)] if cold else [(0,0),(100,0)]+[(50,o) for o in range(7)]
   if trial:plans.reverse()
   for miss,order in plans:
    for arm in arms:
     # Original real-library loops, and unchanged table capacities, are controls.
     impl=arm if arm in ['boost','abseil'] else 'ikea'
     binary=a.binaries/(f'{arm}-memory_ikea_{w}_{b}_{v}' if impl=='ikea' else f'original-memory_peers_{w}_{v}')
     stem=f'{case}-v{v}-s{seed}-t{trial}-{arm}-m{miss}-o{order}'
     command=['taskset','-c',a.cpu,str(binary),'--impl',impl,'--n',str(n),'--reserve',str(n*2 if arm=='abseil' else n),'--seed',str(seed),'--requests','1000000','--repetitions','3','--writes','0','--read-prefetch','33' if cold else '0','--conditioning',str(256*1024**2 if cold else 0),'--miss-only',str(miss),'--query-order',str(order),'--pmu','1']
     start=time.time()
     with (a.output/(stem+'.csv')).open('w') as out,(a.output/(stem+'.jsonl')).open('w') as err:child=subprocess.run(command,stdout=out,stderr=err)
     r=dict(case=case,w=w,v=v,b=b if impl=='ikea' else 0,arm=arm,impl=impl,seed=seed,trial=trial,miss=miss,order=order,command=command,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),start=start,end=time.time(),exit=child.returncode,file=stem+'.csv',counters=stem+'.jsonl')
     records.append(r);(a.output/'runs.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(r),flush=True)
     assert child.returncode==0,r
     rows=[x for x in csv.DictReader((a.output/r['file']).open()) if x['phase']=='read'];assert len(rows)==(18 if cold and arm!='boost' else (9 if cold else 3)),r
     counters=[json.loads(x) for x in (a.output/r['counters']).read_text().splitlines()];assert len(counters)==len(rows),r
print(f'{len(records)} processes complete',flush=True)
