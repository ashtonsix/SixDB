#!/usr/bin/env python3
import csv,hashlib,importlib.util,json,random,statistics,sys
from pathlib import Path
source,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
script=Path(__file__).with_name('report.py')
spec=importlib.util.spec_from_file_location('frozen_report',script);report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
rows=report.summarize(source/'reduction.csv');assert len(rows)==3240
assert len(list(csv.DictReader((source/'reduction.csv').open())))==29160
processes=list(csv.DictReader((source/'processes.csv').open()));cases=[(k,c,a,l) for k in range(8,65,8) for c in range(3) for a in (0,1,8,9) for l in ((0,) if k==8 else (0,1))]
rng=random.Random(127);rng.shuffle(cases);expected=[]
for pair,(k,c,a,l) in enumerate(cases):
 forms=['borrowed','local'];rng.shuffle(forms)
 for within,form in enumerate(forms):expected.append(dict(order=str(len(expected)),pair=str(pair),within_pair=str(within),contender=form,K=str(k),kind=str(c),arm=str(a),large=str(l)))
assert processes==expected
def emit(name,data):
 with (out/name).open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in data for k in r)));w.writeheader();w.writerows(data)
emit('cells.csv',rows)
lookup={(r['contender'],r['K'],r['kind'],r['A'],r['arm'],r['phase']):r for r in rows}
compact=[]
for form,k,c,a,arm in sorted({key[:-1] for key in lookup}):
 r=lookup[(form,k,c,a,arm,'hit')];entry={n:r[n] for n in ['contender','K','kind','A','arm','name','n','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']}
 for phase in ['construct','build','hit','miss','serial_caller_cycle' if c==0 else 'serial_value_cycle','update','erase','release','lifecycle']:
  r=lookup[(form,k,c,a,arm,phase)];short='serial' if phase.startswith('serial') else phase
  for field in ['ns_per_operation','minimum','maximum','operations','ratio_to_borrowed','ratio_to_abseil','ratio_to_abseil_exact']:entry[short+'_'+field]=r[field]
 for phase,stage in [('hit','initial'),('update','updated'),('erase','erased')]:
  r=lookup[(form,k,c,a,arm,phase)]
  for field in ['requested','owned_capacity','metadata']:entry[stage+'_'+field]=r[field]
  entry[stage+'_logical_bytes']=r['requested']+r['metadata']
 compact.append(entry)
emit('comparison.csv',compact)
ranges=[]
for form in report.CONTENDERS:
 for arm in report.ARMS:
  for phase in ['construct','build','hit','miss','serial_caller_cycle','serial_value_cycle','update','erase','release','lifecycle']:
   chosen=[r for r in rows if r['contender']==form and r['arm']==arm and r['phase']==phase]
   for baseline in ['borrowed','abseil','abseil_exact']:
    v=[r['ratio_to_'+baseline] for r in chosen];ranges.append(dict(contender=form,arm=arm,name=report.ARMS[arm],phase=phase,baseline=baseline,cases=len(v),minimum=min(v),median=statistics.median(v),maximum=max(v),at_or_below_one=sum(x<=1 for x in v)))
emit('ranges.csv',ranges)
print(source,'coverage/randomized adjacent pairs/metadata all verified',len(rows),'cells,',len(processes),'processes')
for arm in report.ARMS:
 print(report.ARMS[arm],[(p,round(statistics.median(r['ratio_to_borrowed'] for r in rows if r['contender']=='local' and r['arm']==arm and r['phase']==p),3)) for p in ['hit','miss','serial_value_cycle','build','update','erase','lifecycle']])
