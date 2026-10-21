#!/usr/bin/env python3
"""Audit and regenerate maintained lookup promotion evidence, without a new sweep."""
import argparse,csv,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();here=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(here/'hint-summary.py'),str(a.root/'native'),'--expected','1440'],check=True)
subprocess.run([sys.executable,str(here/'codegen.py'),str(a.root),'--output',str(a.root/'codegen'),'--trim-separators','--precise-names'],check=True)
rr=list(csv.DictReader((a.root/'native/summary.csv').open()))
def row(case,v,arm,miss,order=0,pf=0):
 return next(r for r in rr if (r['case'],int(r['v']),r['arm'],int(r['miss']),int(r['order']),int(r['prefetch']))==(case,v,arm,miss,order,pf))
def ns(*args):return float(row(*args)['ns'])
arms=['original','predictable','unpredictable','adaptive','boost','abseil']
lines=['# Maintained hints and automatic selection','',
 'Native Zen 5 AVX-512 whole-table independent reads, ns/lookup. Each cell: median of 18 intervals in six fresh processes (three seeds, two trials). Actual Boost 1.92 and pinned Abseil keep their original calling loops. Original is the unchanged Ikea callback loop. Maintained arms use the public getter and, for selected, public caller-owned lookup_selector. All consume complete hit/key/value results. Input sequence and table-owned memory match across Ikea arms.',
 '', '| W | Value bytes | Miss % | Original | Default P | Static U | Automatic | Boost | Abseil |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for miss in [0,50,100]:lines.append(f'| {w} | {v} | {miss} | '+' | '.join(f'{ns(case,v,arm,miss):.3f}' for arm in arms)+' |')
lines+=['','## Predictable patterns and changing costs','',
 '| W | Value bytes | Order | Original | Default P | Static U | Automatic | U % | Switches |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
labels={1:'alternating',2:'64-runs',3:'grouped',4:'131071-phase',5:'16381-phase',6:'509-phase'}
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for order in range(1,7):
   r=row(case,v,'adaptive',50,order)
   lines.append(f'| {w} | {v} | {labels[order]} | '+' | '.join(f'{ns(case,v,arm,50,order):.3f}' for arm in arms[:4])+f" | {100*float(r['selected_u']):.2f} | {float(r['switches']):.0f} |")
lines+=['','## Cold independent reads','',
 'Population 14,260,633; 256 MiB conditioning outside each timed interval. PF32 is full-stream home lookahead for Ikea/Abseil; Boost exposes native internal candidate prefetch only. Its native result is repeated in PF32 rows. No per-block reprime. Phased streams use the prior exact order and retained offset; the selector cannot inspect the order label or membership.',
 '', '| W | Value bytes | Prefetch | Miss % | Original | Default P | Static U | Automatic | Boost native | Abseil | U % |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('cold40',40),('cold50',50)]:
 for v in [0,8]:
  for pf in [0,32]:
   for miss in [0,50,100]:
    vals=[ns(case,v,arm,miss,0,pf) for arm in arms[:4]]+[ns(case,v,'boost',miss),ns(case,v,'abseil',miss,0,pf)]
    r=row(case,v,'adaptive',miss,0,pf)
    lines.append(f'| {w} | {v} | {pf} | {miss} | '+' | '.join(f'{x:.3f}' for x in vals)+f" | {100*float(r['selected_u']):.2f} |")
lines+=['','## Random-read counters','',
 '| W | Value bytes | Arm | ns | Cycles/op | Instructions/op | Branch misses/op | U % | Probes/1M |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for arm in arms:
   r=row(case,v,arm,50)
   lines.append(f'| {w} | {v} | {arm} | '+' | '.join(f'{float(r[k]):.5f}' for k in ['ns','cpu_cycles','retired_instructions','branch_misses'])+f" | {100*float(r['selected_u']):.3f} | {float(r['probes']):.0f} |")
subprocess.run([sys.executable,str(here/'hint-stream-summary.py'),str(a.root/'streams/native')],check=True)
small=list(csv.DictReader((a.root/'streams/native/summary.csv').open()))
lines+=['','## Maintained controller: external batch overhead','',
 'Separate hot Ikea-only seam study, six fresh processes (three seeds/type), two counterbalanced trials and three repetitions/method inside each process. Each interval processes 262,144 useful requests in identical preallocated batches; calls and controller integration are timed, input creation is not. State persists across batches. No PMU here; actual vendor controls are above.',
 '', '| Value bytes | Batch | Original hit | Default P hit | Automatic hit | Original miss | Automatic miss | Original random | Automatic random |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
def sm(v,b,p,m):return float(next(r for r in small if tuple(int(r[k]) for k in ['v','batch','pattern','method'])==(v,b,p,m))['ns'])
for v in [0,8]:
 for b in [1,8,32,64,256,1024,4096]:
  vals=[sm(v,b,0,m) for m in [0,1,3]]+[sm(v,b,1,m) for m in [0,3]]+[sm(v,b,2,m) for m in [0,3]]
  lines.append(f'| {v} | {b} | '+' | '.join(f'{x:.3f}' for x in vals)+' |')
(a.root/'tables.md').write_text('\n'.join(lines)+'\n')
preservation={}
for arm in ['predictable','adaptive']:
 candidates=[]
 for r in rr:
  if r['arm']!=arm:continue
  if not (r['case'].startswith('cold') or r['miss']!='50' or int(r['order']) in [1,2,3]):continue
  baseline=row(r['case'],int(r['v']),'original',int(r['miss']),int(r['order']),int(r['prefetch']))
  candidates.append(dict(case=r['case'],v=int(r['v']),miss=int(r['miss']),order=int(r['order']),prefetch=int(r['prefetch']),ns=float(r['ns']),original_ns=float(baseline['ns']),ratio=float(r['ns'])/float(baseline['ns'])))
 preservation[arm]=max(candidates,key=lambda r:r['ratio'])
audit=json.loads((a.root/'native/audit.json').read_text())
audit['streams']=json.loads((a.root/'streams/native/audit.json').read_text())
audit.update(w32_budget={f'v{v}-{arm}':dict(hit_ns=ns('hot32',v,arm,0),miss_ns=ns('hot32',v,arm,100),random_ns=ns('hot32',v,arm,50),random_mqps=1000/ns('hot32',v,arm,50)) for v in [0,8] for arm in ['predictable','adaptive']},worst_nonrandom_or_cold_ratio=preservation,selector_bytes=64,targets_met=dict(selected_w32_hit_under_1_6=all(ns('hot32',v,'adaptive',0)<1.6 for v in [0,8]),selected_w32_random_over_220_mqps=all(1000/ns('hot32',v,'adaptive',50)>220 for v in [0,8])))
(a.root/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(a.root/'tables.md')
