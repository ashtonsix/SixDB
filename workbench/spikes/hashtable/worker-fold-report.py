#!/usr/bin/env python3
"""Qualify bounded cross-Worker fold throughput, lifetime and separate memory."""
import argparse,csv,hashlib,json,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
files=sorted(a.root.glob('hashtable_design_worker_fold_*-order*.csv'));assert files
ids=['K','kind','W','keys','passes','slot_family','overlap','arm']
groups={};hashes={}
repaired_headers=[]
for file in files:
 with file.open() as f:
  reader=csv.reader(f);header=next(reader);data=list(reader)
 # V1 emitted all three final accounting values but omitted their header
 # labels. Recover only that exact schema; never silently discard CSV fields.
 if header[-1]=='slot_family' and data and all(len(r)==len(header)+3 for r in data):
  header+=['inactive_bytes','context_bytes','inspection_bytes'];repaired_headers.append(file.name)
 assert data and all(len(r)==len(header) for r in data),(file,'CSV width')
 rows=[dict(zip(header,r)) for r in data]
 hashes[file.name]=hashlib.sha256(file.read_bytes()).hexdigest()
 assert len({tuple(r[k] for k in ids[:6]) for r in rows})==1
 reps={int(r['rep']) for r in rows};assert reps==set(range(len(reps)))
 expected={(str(o),str(r),a) for o in [0,50,100] for r in reps for a in ['hierarchy','abseil']};seen=set()
 for r in rows:
  cell=tuple(r[k] for k in ['overlap','rep','arm']);assert cell not in seen;seen.add(cell)
  count=int(r['keys'])*(int(r['passes']) if r['kind']=='2' else 1)
  assert int(r['transferred'])==count
  assert 0<int(r['chunks'])<=count and int(r['chunks'])*64>=count
  assert int(r['source_work'])>=count
  assert int(r['cancelled'])==0
  assert 0<=int(r['source_epoch'])<=int(r['destination_after'])<=3
  assert r['source_epoch']==r['destination_before']
  assert int(r['logical'])<=int(r['coexist_logical'])
  assert 0<=int(r['partial'])<=1
  if r['arm']=='abseil':assert int(r['backing'])==-1 and int(r['destination_after'])==0
  if r['K']=='8':assert int(r['destination_after'])==0
  total=sum(float(r[k]) for k in ['setup_ns','build_ns','fold_ns','source_retire_ns','retire_ns'])
  assert abs(total-float(r['lifetime_ns']))<.01
  assert abs(float(r['fold_ns'])/count-float(r['fold_ns_per_input']))<.001
  groups.setdefault(tuple(r[k] for k in ids),[]).append(r)
 assert seen==expected,(file,seen^expected)
summary=[]
for key,rows in sorted(groups.items()):
 out=dict(zip(ids,key));out['samples']=len(rows)
 for field in rows[0]:
  if field in ids or field=='rep':continue
  v=[float(r[field]) for r in rows];out[field]=statistics.median(v);out[field+'_min']=min(v);out[field+'_max']=max(v)
 summary.append(out)
a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'summary.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
(a.output/'inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
(a.output/'schema-recovery.json').write_text(json.dumps({'omitted_v1_header_labels':repaired_headers},indent=2)+'\n')
print(f'{len(files)} complete processes; {sum(map(len,groups.values()))} rows; {len(summary)} fold/lifetime/memory cells. No serial-latency, exact peak or RSS claim.')
