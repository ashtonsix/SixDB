#!/usr/bin/env python3
"""Validate finite one/two-inline native histories and retain fixed-arm comparisons.
Usage: compare.py RESULTS OUTPUT [--repetitions N]
The mixed workload and each uniform fanout are separate strata, never pooled.
"""
import argparse,csv,json,random,statistics,sys
from pathlib import Path
import report
p=argparse.ArgumentParser(__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--repetitions',type=int,default=9);args=p.parse_args();source=args.source;out=args.output;out.mkdir(parents=True,exist_ok=True)
fanouts=[0,1,2,3,4,8,32,128];rows=[];checks=[]
for f in fanouts:
 filename='formats.csv' if f==0 else f'fanout-{f}.csv';summary='summary.json' if f==0 else f'fanout-{f}-summary.json'
 panel=report.summarize(source/filename,complete=f==0,inline=True)
 assert panel==json.loads((source/summary).read_text()),filename
 assert all(r['fanout']==f and r['repetitions']==args.repetitions for r in panel)
 cases={(k,c,a) for k in range(8,65,8) for c in range(3) for a in ((6,) if k==8 else (6,10))} if f==0 else {(k,2,a) for k in [16,40,64] for a in [6,10]}
 assert {(r['K'],r['kind'],r['A']) for r in panel}==cases
 assert len(panel)==len(cases)*8*9
 raw=list(csv.DictReader((source/filename).open()));assert len(raw)==len(panel)*args.repetitions
 single={tuple(r[n] for n in ['K','kind','A','arm','phase','rep']):r for r in raw};assert len(single)==len(raw)
 fields=['n','operations','requested','metadata','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']
 for key,r in single.items():
  k,c,a,arm,phase,rep=map(str,key)
  if int(arm)>=6:continue
  # Same physical representation must match between composed and Direct paths.
  ref_arm=str(int(arm)//2*2) if c=='2' else '0';ref=single[(k,c,a,ref_arm,phase,rep)]
  for field in fields:assert r[field]==ref[field],(f,key,field,r[field],ref[field])
  assert sum(int(r[n]) for n in ['build_primary_rows','build_secondary_rows','build_overflow_rows'])==int(r['n'])
 if f==0:
  order=[(k,c,a,l) for k in range(8,65,8) for c in range(3) for a in range(8) for l in ((0,) if k==8 else (0,1))];random.Random(127).shuffle(order)
  assert list(csv.DictReader((source/'processes.csv').open()))==[dict(order=str(i),K=str(k),kind=str(c),arm=str(a),large=str(l)) for i,(k,c,a,l) in enumerate(order)]
 else:
  order=[(k,a,l) for k in [16,40,64] for a in range(8) for l in [0,1]];random.Random(127+f).shuffle(order)
  receipts=json.loads((source/f'fanout-{f}-processes.json').read_text());assert len(receipts)==len(order)
  for r,(k,a,l) in zip(receipts,order):
   assert r['command'][4:]==['run',str(k),'2',str(a),str(args.repetitions),str(l),str(f)]
   assert r['end_ns']>=r['start_ns']
 rows+=panel;checks.append(dict(fanout=f,cases=len(cases),arms=8,phases=9,repetitions=args.repetitions,raw_rows=len(raw),processes=len(order)))
lookup={(r['K'],r['kind'],r['A'],r['arm'],r['fanout'],r['phase']):r for r in rows}
def emit(name,data):
 with (out/name).open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in data for k in r)));w.writeheader();w.writerows(data)
compact=[]
for k,c,a,arm,f in sorted({key[:-1] for key in lookup}):
 key=(k,c,a,arm,f);init=lookup[(*key,'hit')];row={n:init[n] for n in ['K','kind','A','arm','fanout','name','n','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']}
 n=init['n'];row['initial_values']=n if c!=2 else n*f if f else n+(n+2)//3;row['updated_values']=row['initial_values']+(n if c==2 else 0)
 if arm<6:
  ref=lookup[(k,c,a,0,f,'hit')];p=q=128
  if c==2 and arm>=4:p=((128+8*ref['primary_slots']+63)//64)*64;q=((128+8*ref['secondary_slots']+63)//64)*64
  row.update(inline_values=2 if c==2 and arm>=2 else 1,primary_unit_bytes=p,secondary_unit_bytes=q,primary_load=init['build_primary_rows']/(init['primary_slots']*(1<<a)),secondary_load=init['build_secondary_rows']/(init['secondary_slots']*(1<<max(0,a-4))),key_overflow_fraction=init['build_overflow_rows']/n)
 for phase in ['construct','build','hit','miss','serial_caller_cycle' if c==0 else 'serial_value_cycle','update','erase','release','lifecycle']:
  r=lookup[(*key,phase)];short='serial' if phase.startswith('serial') else phase
  for field in ['ns_per_operation','minimum','maximum','operations','ratio_to_abseil','ratio_to_abseil_exact']:row[short+'_'+field]=r[field]
  if arm<6:row[short+'_ratio_to_same_codec_inline1']=r['ns_per_operation']/lookup[(k,c,a,arm%2,f,phase)]['ns_per_operation']
 for phase,stage in [('hit','initial'),('update','updated'),('erase','erased')]:
  r=lookup[(*key,phase)];logical=r['requested']+r['metadata'];row[stage+'_logical_bytes']=logical;row[stage+'_logical_bytes_per_initial_key']=logical/n
  for field in ['requested','metadata','owned_capacity']:row[stage+'_'+field]=r[field]
  for refarm,label in [(0,'inline1'),(6,'abseil'),(7,'abseil_exact')]:
   ref=lookup[(k,c,a,refarm,f,phase)];row[stage+'_logical_ratio_to_'+label]=logical/(ref['requested']+ref['metadata'])
 compact.append(row)
emit('comparison.csv',compact)
ranges=[]
for f in fanouts:
 for c in ([0,1,2] if f==0 else [2]):
  for arm in range(6):
   for phase in ['hit','miss','serial','update','build','erase','lifecycle']:
    selected=[r for r in compact if r['fanout']==f and r['kind']==c and r['arm']==arm]
    for baseline in ['abseil','abseil_exact','same_codec_inline1']:
     v=[r[phase+'_ratio_to_'+baseline] for r in selected]
     ranges.append(dict(fanout=f,kind=c,arm=arm,name=report.INLINE_NAMES[arm],phase=phase,baseline=baseline,cases=len(v),minimum=min(v),median=statistics.median(v),maximum=max(v),at_or_below_one=sum(x<=1 for x in v)))
emit('ranges.csv',ranges);(out/'coverage.json').write_text(json.dumps(checks,indent=2)+'\n')
print('Verified',sum(x['raw_rows'] for x in checks),'rows,',sum(x['processes'] for x in checks),'processes; all fanouts, pairwise memory/population controls and shuffled schedules match')
