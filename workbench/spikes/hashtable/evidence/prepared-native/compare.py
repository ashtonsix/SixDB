#!/usr/bin/env python3
"""Validate all prepared-owner cases and regenerate fixed-arm comparisons.
Usage: compare.py EVIDENCE_DIR OUTPUT_DIR [CODEGEN_DIR]
No claim of matched-byte causality against a different source cohort.
"""
import csv,json,random,statistics,sys
from pathlib import Path
import report
source,out=map(Path,sys.argv[1:3]);out.mkdir(parents=True,exist_ok=True)
rows=report.summarize(source/'formats.csv',True,True)
assert len(rows)==4050 and all(r['repetitions']==9 for r in rows)
raw=list(csv.DictReader((source/'formats.csv').open()));assert len(raw)==36450
individual={tuple(r[k] for k in ['K','kind','A','arm','phase','rep']):r for r in raw};assert len(individual)==len(raw)
for key,r in individual.items():
 if int(r['arm'])<8:
  ref=individual[(*key[:3],'0',*key[4:])]
  for field in ['n','operations','requested','metadata','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']:assert r[field]==ref[field],(key,field,r[field],ref[field])
  assert sum(int(r[k]) for k in ['build_primary_rows','build_secondary_rows','build_overflow_rows'])==int(r['n'])
cases=[(k,c,a,l) for k in range(8,65,8) for c in range(3) for a in range(10) for l in ((0,) if k==8 else (0,1))];random.Random(127).shuffle(cases)
assert list(csv.DictReader((source/'processes.csv').open()))==[dict(order=str(i),K=str(k),kind=str(c),arm=str(a),large=str(l)) for i,(k,c,a,l) in enumerate(cases)]
lookup={(r['K'],r['kind'],r['A'],r['arm'],r['phase']):r for r in rows}
def emit(name,data):
 with (out/name).open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in data for k in r)));w.writeheader();w.writerows(data)
compact=[]
for k,c,a,arm in sorted({key[:-1] for key in lookup}):
 key=(k,c,a,arm);initial=lookup[(*key,'hit')]
 row={n:initial[n] for n in ['K','kind','A','arm','name','n','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']}
 for phase in ['construct','build','hit','miss','serial_caller_cycle' if c==0 else 'serial_value_cycle','update','erase','release','lifecycle']:
  r=lookup[(*key,phase)];short='serial' if phase.startswith('serial') else phase
  for field in ['ns_per_operation','minimum','maximum','operations','ratio_to_abseil','ratio_to_abseil_exact']:row[short+'_'+field]=r[field]
  if arm<8:row[short+'_ratio_to_same_codec_exact']=r['ns_per_operation']/lookup[(k,c,a,arm%2,phase)]['ns_per_operation']
 for phase,stage in [('hit','initial'),('update','updated'),('erase','erased')]:
  r=lookup[(*key,phase)]
  for field in ['requested','owned_capacity','metadata']:row[stage+'_'+field]=r[field]
  row[stage+'_logical_bytes']=r['requested']+r['metadata']
  for b in [8,9]:
   ref=lookup[(k,c,a,b,phase)];row[stage+'_logical_ratio_to_'+report.ACCESS_NAMES[b]]=(r['requested']+r['metadata'])/(ref['requested']+ref['metadata'])
 compact.append(row)
emit('comparison.csv',compact)
ranges=[]
for arm in range(8):
 for phase in ['construct','build','hit','miss','serial_value_cycle','serial_caller_cycle','update','erase','release','lifecycle']:
  for baseline in ['abseil','abseil_exact','same_codec_exact']:
   cells=[r for r in rows if r['arm']==arm and r['phase']==phase]
   v=[r['ns_per_operation']/lookup[(r['K'],r['kind'],r['A'],arm%2,phase)]['ns_per_operation'] if baseline=='same_codec_exact' else r['ratio_to_'+baseline] for r in cells]
   ranges.append(dict(arm=arm,name=report.ACCESS_NAMES[arm],phase=phase,baseline=baseline,cases=len(v),minimum=min(v),median=statistics.median(v),maximum=max(v),at_or_below_one=sum(x<=1 for x in v)))
emit('ranges.csv',ranges)
if len(sys.argv)>3:
 code=list(csv.DictReader((Path(sys.argv[3])/'codegen.csv').open()));assert len(code)==1800
 helpers={tuple(int(r[k]) for k in ['K','kind','A','arm'])+(r['phase'],):r for r in code if r.get('helper_address')}
 identical=[]
 for (k,c,a,arm,phase),r in helpers.items():
  if arm<2 or arm>=8:continue
  ref=helpers.get((k,c,a,arm%2,phase))
  if ref is None or ref['helper_normalized_sha256']!=r['helper_normalized_sha256']:continue
  p=('serial_caller_cycle' if c==0 else 'serial_value_cycle') if phase=='serial' else phase
  value=lookup[(k,c,a,arm,p)]['ns_per_operation'];denominator=lookup[(k,c,a,arm%2,p)]['ns_per_operation']
  identical.append(dict(K=k,kind=c,A=a,arm=arm,phase=phase,control_arm=arm%2,ratio=value/denominator,ns_per_operation=value,control_ns_per_operation=denominator,helper_address=r['helper_address'],control_address=ref['helper_address'],normalized_sha256=r['helper_normalized_sha256']))
 emit('identical-code-controls.csv',identical)
print('Verified all 36450 rows, 4050 cells, randomized450-process order, per-rep memory/population equality among candidate arms')
