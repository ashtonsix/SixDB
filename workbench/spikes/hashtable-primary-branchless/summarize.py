#!/usr/bin/env python3
"""Audit useful results, owned bytes, binary identity and complete PMU groups."""
import argparse,collections,csv,hashlib,json,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--expected',type=int,required=True);a=p.parse_args()
records=json.loads((a.root/'runs.json').read_text());assert len(records)==a.expected
answers={};hashes={};counts=collections.Counter();groups=collections.defaultdict(list);statuses=collections.Counter();bytes_by_case={}
events=['cpu_cycles','retired_instructions','branches','branch_misses']
for rec in records:
 assert rec['exit']==0
 binary=a.root.parent/'binaries'/Path(rec['command'][3]).name
 if binary not in hashes: hashes[binary]=hashlib.sha256(binary.read_bytes()).hexdigest()
 assert hashes[binary]==rec['binary_sha256']
 rows=list(csv.DictReader((a.root/rec['file']).open()));reads=[r for r in rows if r['phase']=='read']
 counters=[json.loads(l) for l in (a.root/rec['counters']).read_text().splitlines()];assert len(reads)==len(counters)
 build,=[r for r in rows if r['phase']=='build']
 seen=set()
 for row,pmu in zip(reads,counters):
  miss=int(row['miss']);pf=int(row['prefetch']);nops=int(row['operations']);assert nops==1000000
  identity=(rec['case'],rec['v'],rec['arm'],miss,pf);seen.add(identity)
  answer=tuple(int(row[k]) for k in ['hits','keys','values'])
  k=(rec['case'],rec['v'],rec['seed'],miss);assert answers.setdefault(k,answer)==answer,(rec,k,answer)
  if miss==0:assert answer[0]==nops
  if miss==100:assert answer==(0,0,0)
  if rec['impl']=='ikea':
   k=(rec['case'],rec['v'],rec['seed']);assert bytes_by_case.setdefault(k,int(row['bytes']))==int(row['bytes'])
  statuses[pmu['status']]+=1;assert pmu['status']=='complete' and pmu['coverage']==1 and not pmu['multiplexed'] and not pmu['scaled'],pmu
  event={e['name']:e for e in pmu['events']}
  sample=dict(ns=float(row['ns']),bytes=int(row['bytes']),spill=int(build['spill']),seed=rec['seed'])
  for name in events:
   e=event[name];assert e['status']=='measured' and e['count'] is not None
   sample[name]=e['count']/nops
  groups[identity].append(sample)
 for k in seen:counts[k]+=1
assert set(counts.values())=={6}
fields=['case','v','arm','miss','prefetch','ns','mad','mops','samples','processes','seeds','bytes','spill_median',*events]
with (a.root/'summary.csv').open('w') as out:
 writer=csv.DictWriter(out,fieldnames=fields,lineterminator='\n');writer.writeheader()
 for identity,samples in sorted(groups.items()):
  ns=statistics.median(r['ns'] for r in samples)
  row=dict(zip(fields[:5],identity),ns=ns,mad=statistics.median(abs(r['ns']-ns) for r in samples),mops=1000/ns,samples=len(samples),processes=counts[identity],seeds=len({r['seed'] for r in samples}))
  for name in ['bytes','spill',*events]:row['spill_median' if name=='spill' else name]=statistics.median(r[name] for r in samples)
  writer.writerow(row)
audit=dict(processes=len(records),read_intervals=sum(len(v) for v in groups.values()),result_identities=len(answers),verified_binaries=len(hashes),pmu_statuses=dict(statuses),all_counts_unscaled=True,coverage=1,same_answers_across_libraries_variants_prefetch=True,same_owned_bytes_across_ikea_variants=True)
(a.root/'audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit,indent=2))
