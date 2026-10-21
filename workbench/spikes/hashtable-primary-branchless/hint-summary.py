#!/usr/bin/env python3
"""Audit exact sequences, useful results and cost of explicit/adaptive hints."""
import argparse,collections,csv,hashlib,json,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--expected',type=int,required=True);a=p.parse_args()
records=json.loads((a.root/'runs.json').read_text());assert len(records)==a.expected
answers={};sequences={};binary_hashes={};groups=collections.defaultdict(list);counts=collections.Counter();pmu_counts=collections.Counter();owned={}
events=['cpu_cycles','retired_instructions','branches','branch_misses']
for rec in records:
 assert rec['exit']==0,rec
 binary=a.root.parent/'binaries'/Path(rec['command'][3]).name
 if binary not in binary_hashes:binary_hashes[binary]=hashlib.sha256(binary.read_bytes()).hexdigest()
 assert binary_hashes[binary]==rec['binary_sha256']
 rows=list(csv.DictReader((a.root/rec['file']).open()));reads=[r for r in rows if r['phase']=='read']
 counters=[json.loads(x) for x in (a.root/rec['counters']).read_text().splitlines()];assert len(reads)==len(counters)
 seen=set()
 for r,pmu in zip(reads,counters):
  n=int(r['operations']);assert n==1_000_000 and int(r['query_order'])==rec['order']
  if rec['miss']!=101:assert int(r['miss'])==rec['miss']
  key=(rec['case'],rec['v'],rec['seed'],int(r['miss']));answer=tuple(int(r[k]) for k in ['hits','keys','values'])
  assert answers.setdefault(key,answer)==answer,(key,rec)
  seqkey=(*key,rec['order']);assert sequences.setdefault(seqkey,int(r['query_hash']))==int(r['query_hash']),(seqkey,rec)
  if int(r['miss'])==0:assert answer[0]==n
  if int(r['miss'])==100:assert answer==(0,0,0)
  selected_p=int(r['method_predictable']);selected_u=int(r['method_unpredictable']);probes=int(r['selector_probes']);switches=int(r['selector_switches'])
  if rec['arm']=='predictable':assert (selected_p,selected_u,probes,switches)==(n,0,0,0)
  if rec['arm']=='unpredictable':assert (selected_p,selected_u,probes,switches)==(0,n,0,0)
  if rec['arm']=='adaptive':assert selected_p+selected_u==n and 0<probes<n//32 and switches<=probes
  if rec['impl']=='ikea':
   memkey=(rec['case'],rec['v'],rec['seed']);assert owned.setdefault(memkey,int(r['bytes']))==int(r['bytes'])
  assert pmu['status']=='complete' and pmu['coverage']==1 and not pmu['scaled'] and not pmu['multiplexed'],pmu
  pmu_counts[pmu['status']]+=1;ev={e['name']:e for e in pmu['events']}
  identity=(rec['case'],rec['v'],rec['arm'],int(r['miss']),rec['order'],int(r['prefetch']));seen.add(identity)
  sample=dict(ns=float(r['ns']),seed=rec['seed'],bytes=int(r['bytes']),selected_p=selected_p/n,selected_u=selected_u/n,probes=probes,switches=switches)
  for name in events:
   assert ev[name]['status']=='measured' and ev[name]['count'] is not None
   sample[name]=ev[name]['count']/n
  groups[identity].append(sample)
 for k in seen:counts[k]+=1
assert set(counts.values())=={6},counts
for case,v,seed,miss in answers:
 if case.startswith('hot') and miss==50:
  assert len({sequences[case,v,seed,miss,o] for o in range(7)})==7

fields=['case','v','arm','miss','order','prefetch','ns','mad','mops','processes','intervals','seeds','bytes','selected_p','selected_u','probes','switches',*events]
with (a.root/'summary.csv').open('w') as out:
 w=csv.DictWriter(out,fieldnames=fields,lineterminator='\n');w.writeheader()
 for key,rr in sorted(groups.items()):
  ns=statistics.median(r['ns'] for r in rr);row=dict(zip(fields[:6],key),ns=ns,mad=statistics.median(abs(r['ns']-ns) for r in rr),mops=1000/ns,processes=counts[key],intervals=len(rr),seeds=len({r['seed'] for r in rr}))
  for name in ['bytes','selected_p','selected_u','probes','switches',*events]:row[name]=statistics.median(r[name] for r in rr)
  w.writerow(row)
audit=dict(processes=len(records),intervals=sum(len(v) for v in groups.values()),answer_identities=len(answers),exact_sequence_identities=len(sequences),binaries=len(binary_hashes),pmu_statuses=dict(pmu_counts),coverage=1,all_counters_unscaled=True,same_answers_and_query_sequences_across_all_arms=True,same_owned_bytes_across_ikea_arms=True)
(a.root/'audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit,indent=2))
