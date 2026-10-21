#!/usr/bin/env python3
"""Summarize the separate hot microbatch tax study (three seed processes/type)."""
import argparse,collections,csv,json,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();groups=collections.defaultdict(list);answers={};files=sorted(a.root.glob('v*-s*.csv'));assert len(files)==6
for path in files:
 rows=list(csv.DictReader(path.open()));assert len(rows)==7*5*2*4*3
 for r in rows:
  v,batch,pattern,method=map(lambda k:int(r[k]),['v','batch','pattern','method'])
  answer=tuple(int(r[k]) for k in ['hits','keys','values']);key=(v,int(r['seed']),pattern)
  assert answers.setdefault(key,answer)==answer,(path,key)
  if pattern==0:assert answer[0]==262144
  if pattern==1:assert answer==(0,0,0)
  if method==3:assert int(r['p'])+int(r['u'])==262144
  groups[v,batch,pattern,method].append(r)
fields=['v','batch','pattern','method','ns','mad','intervals','seeds','selected_u','probes','switches']
with (a.root/'summary.csv').open('w') as out:
 writer=csv.DictWriter(out,fieldnames=fields,lineterminator='\n');writer.writeheader()
 for key,rr in sorted(groups.items()):
  assert len(rr)==18
  ns=statistics.median(float(r['ns']) for r in rr)
  row=dict(zip(fields[:4],key),ns=ns,mad=statistics.median(abs(float(r['ns'])-ns) for r in rr),intervals=len(rr),seeds=len({r['seed'] for r in rr}),selected_u=statistics.median(int(r['u'])/262144 for r in rr),probes=statistics.median(int(r['probes']) for r in rr),switches=statistics.median(int(r['switches']) for r in rr));writer.writerow(row)
audit=dict(fresh_processes=len(files),intervals=sum(len(x) for x in groups.values()),answer_identities=len(answers),same_answers_across_methods_and_batch_sizes=True,requests_per_interval=262144,pmu=False,scope='hot Ikea-only seam tax; vendor controls are in the full cohort')
(a.root/'audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit,indent=2))
