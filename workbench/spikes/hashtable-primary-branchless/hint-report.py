#!/usr/bin/env python3
"""Regenerate the predictable-first decision from captured native reads."""
import argparse,csv,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();here=Path(__file__).resolve().parent
for cohort,count in [('pilot',648),('full',1440)]:
 subprocess.run([sys.executable,str(here/'hint-summary.py'),str(a.root/cohort/'native'),'--expected',str(count)],check=True,stdout=subprocess.DEVNULL)
subprocess.run([sys.executable,str(here/'hint-stream-summary.py'),str(a.root/'streams/native')],check=True,stdout=subprocess.DEVNULL)
subprocess.run([sys.executable,str(here/'codegen.py'),str(a.root/'full'),'--output',str(a.root/'full/codegen'),'--trim-separators'],check=True,stdout=subprocess.DEVNULL)
rr=list(csv.DictReader((a.root/'full/native/summary.csv').open()));small=list(csv.DictReader((a.root/'streams/native/summary.csv').open()))
def row(case,v,arm,miss,order=0,pf=0):return next(r for r in rr if (r['case'],int(r['v']),r['arm'],int(r['miss']),int(r['order']),int(r['prefetch']))==(case,v,arm,miss,order,pf))
def ns(*args):return float(row(*args)['ns'])
lines=['# Predictable-first lookup hints: native scorecard','',
 'Native Zen 5 AVX-512 whole-table independent reads, ns/lookup (lower is better). Each full-cohort cell is the median of 18 intervals in six fresh processes, three seeds. All arms consume the complete hit/key/value answer. Original Ikea and the actual Boost/Abseil controls retain their original callback loops; the two getter specializations and selector use the corresponding consumer loop. Inputs, encoded geometry and table-owned memory are fixed.',
 '', '## Hot independent reads','',
 '| W | Value bytes | Miss % | Original | Predictable | Unpredictable | Selected | Boost | Abseil |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for miss in [0,50,100]:lines.append(f'| {w} | {v} | {miss} | '+' | '.join(f'{ns(case,v,arm,miss):.3f}' for arm in ['original','predictable','unpredictable','adaptive','boost','abseil'])+' |')
lines+=['','## Predictable 50/50 and workload shifts','',
 'Ordering controls retain the exact multiset and within-class order. Phased traces rotate through all-hit, random, all-miss and alternating segments. Segment lengths are 131,071 / 16,381 / 509 requests with a 7,759-request offset, so transitions do not line up with the selector window. The selector sees only measured costs, never the phase label or membership oracle.',
 '', '| W | Value bytes | Order | Original | Predictable | Unpredictable | Selected | Selected U % | Switches/trace |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
labels={1:'alternating',2:'64-runs',3:'grouped',4:'131071-phase',5:'16381-phase',6:'509-phase'}
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for order in range(1,7):
   r=row(case,v,'adaptive',50,order)
   lines.append(f'| {w} | {v} | {labels[order]} | '+' | '.join(f'{ns(case,v,arm,50,order):.3f}' for arm in ['original','predictable','unpredictable','adaptive'])+f" | {100*float(r['selected_u']):.2f} | {float(r['switches']):.0f} |")
lines+=['','## Cold independent reads','',
 'Population 14,260,633; each interval follows a 256 MiB conditioning scan. Prefetch32 means home prefetch 32 useful requests ahead for Ikea/Abseil. Boost has native internal candidate prefetch, without that public home hook; its native result is repeated in prefetch32 rows. The selector preserves lookahead across internal block boundaries.',
 '', '| W | Value bytes | Prefetch | Miss % | Original | Predictable | Unpredictable | Selected | Boost native | Abseil | Selected U % |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('cold40',40),('cold50',50)]:
 for v in [0,8]:
  for pf in [0,32]:
   for miss in [0,50,100]:
    vals=[ns(case,v,arm,miss,0,pf) for arm in ['original','predictable','unpredictable','adaptive']]+[ns(case,v,'boost',miss),ns(case,v,'abseil',miss,0,pf)]
    r=row(case,v,'adaptive',miss,0,pf)
    lines.append(f'| {w} | {v} | {pf} | {miss} | '+' | '.join(f'{x:.3f}' for x in vals)+f" | {100*float(r['selected_u']):.2f} |")
lines+=['','## W32 microbatch seam tax','',
 'Separate hot Ikea-only experiment: three fresh seed processes/type, two counterbalanced trials and three repetitions per method. Each interval processes 262,144 useful queries split into preallocated batches. Query generation/allocation is outside timing. Original/predictable/unpredictable/selected use the same immutable batches; selected state persists across them. These timings include per-batch calls and integration, so compare within each row; they are not bare per-get latency. No PMU is collected in this diagnostic. Vendor controls remain in the full study above.',
 '', '| Value bytes | Batch | Original hit | Predictable hit | Selected hit | Original miss | Selected miss | Original random | Selected random |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
def sm(v,b,p,m):return float(next(r for r in small if tuple(int(r[k]) for k in ['v','batch','pattern','method'])==(v,b,p,m))['ns'])
for v in [0,8]:
 for b in [1,8,32,64,256,1024,4096]:
  vals=[sm(v,b,0,m) for m in [0,1,3]]+[sm(v,b,1,m) for m in [0,3]]+[sm(v,b,2,m) for m in [0,3]]
  lines.append(f'| {v} | {b} | '+' | '.join(f'{x:.3f}' for x in vals)+' |')
lines+=['','## Actual mixed-read counter/selector work','',
 '| W | Value bytes | Arm | ns | Cycles/op | Instructions/op | Branch misses/op | U share % | Probes/1M |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for arm in ['original','predictable','unpredictable','adaptive','boost','abseil']:
   r=row(case,v,arm,50)
   lines.append(f'| {w} | {v} | {arm} | '+' | '.join(f'{float(r[k]):.5f}' for k in ['ns','cpu_cycles','retired_instructions','branch_misses'])+f" | {100*float(r['selected_u']):.3f} | {float(r['probes']):.0f} |")
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
budget={}
for v in [0,8]:
 for arm in ['predictable','adaptive']:
  budget[f'v{v}-{arm}']=dict(hit_ns=ns('hot32',v,arm,0),miss_ns=ns('hot32',v,arm,100),random_ns=ns('hot32',v,arm,50),random_mqps=1000/ns('hot32',v,arm,50))
audit={c:json.loads((a.root/c/'native/audit.json').read_text()) for c in ['pilot','full','streams']}
audit.update(targets_met=dict(predictable_w32_hit_under_1_6=all(ns('hot32',v,'predictable',0)<1.6 for v in [0,8]),selected_w32_hit_under_1_6=all(ns('hot32',v,'adaptive',0)<1.6 for v in [0,8]),selected_w32_random_over_220_mqps=all(1000/ns('hot32',v,'adaptive',50)>220 for v in [0,8])),w32_budget=budget,worst_nonrandom_or_cold_ratio=preservation,selector_bytes=64,branch_specialization='compile-time; dispatcher/probes outside per-key loop',getter_contract='copied value+found, zero value on absence; neither hint promises key presence')
(a.root/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(a.root/'tables.md')
