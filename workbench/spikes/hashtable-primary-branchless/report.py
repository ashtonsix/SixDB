#!/usr/bin/env python3
"""Re-audit raw cohorts and render the decision tables without new timing."""
import argparse,csv,json,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args()
study=Path(__file__).resolve().parent
cohorts=[('callback',192),('result',240),('lean',120),('carry',192)]
rows={};audits={}
for name,count in cohorts:
 root=a.root/name
 subprocess.run([sys.executable,str(study/'summarize.py'),str(root/'native'),'--expected',str(count)],check=True,stdout=subprocess.DEVNULL)
 subprocess.run([sys.executable,str(study/'codegen.py'),str(root),'--output',str(root/'codegen')],check=True,stdout=subprocess.DEVNULL)
 rows[name]=list(csv.DictReader((root/'native/summary.csv').open()))
 audits[name]=json.loads((root/'native/audit.json').read_text())
answers={}
for name,_ in cohorts:
 root=a.root/name/'native'
 for rec in json.loads((root/'runs.json').read_text()):
  for row in csv.DictReader((root/rec['file']).open()):
   if row['phase']!='read':continue
   key=(rec['case'],rec['v'],rec['seed'],int(row['miss']))
   answer=tuple(int(row[k]) for k in ['hits','keys','values'])
   assert answers.setdefault(key,answer)==answer,(name,key)
def get(cohort,case,v,arm,miss,pf=0):
 return next(r for r in rows[cohort] if (r['case'],int(r['v']),r['arm'],int(r['miss']),int(r['prefetch']))==(case,v,arm,miss,pf))
def ns(*args):return float(get(*args)['ns'])
out=['# Branchless primary: complete comparisons','',
 'Native Zen 5 AVX-512 independent reads, ns/lookup (lower is better). Each cell is the median of 18 intervals in six fresh processes, three seeds; repetitions within a process are not independent trials.',
 '', 'The callback cohort retains the original consumer. Result, lean and carry cohorts use the same value-returning adapter for all libraries, sum every hit key and every value byte, and verify identical answers. The maintained adapter calls ordinary `find`; candidates alone have the research `lookup`. Geometry and owned bytes do not change across Ikea arms.',
 '', '## Final hot comparison','',
 '| Key bits | Value bytes | Miss % | Original callback | Maintained result adapter | Carry | Boost adapter | Abseil adapter | Carry / maintained adapter |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for miss in [0,50,100]:
   vals=[ns('callback',case,v,'base',miss),ns('carry',case,v,'base',miss),ns('carry',case,v,'carry',miss),ns('carry',case,v,'boost',miss),ns('carry',case,v,'abseil',miss)]
   out.append(f'| {w} | {v} | {miss} | '+' | '.join(f'{x:.3f}' for x in vals)+f' | {vals[2]/vals[1]:.2f}× |')
out+=['','## Final cold comparison','',
 'Same logical population 14,260,633. Each interval follows a 256 MiB cache-conditioning scan. `32` means caller home prefetch 32 requests ahead for Ikea/Abseil; Boost uses its native internal prefetch and has no equivalent public home-address hook. Its native result is repeated in both rows for comparison.',
 '', '| Key bits | Value bytes | Prefetch | Miss % | Original callback | Maintained adapter | Carry | Boost native adapter | Abseil adapter |',
 '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('cold40',40),('cold50',50)]:
 for v in [0,8]:
  for pf in [0,32]:
   for miss in [0,50,100]:
    vals=[ns('callback',case,v,'base',miss,pf),ns('carry',case,v,'base',miss,pf),ns('carry',case,v,'carry',miss,pf),ns('carry',case,v,'boost',miss),ns('carry',case,v,'abseil',miss,pf)]
    out.append(f'| {w} | {v} | {pf} | {miss} | '+' | '.join(f'{x:.3f}' for x in vals)+' |')
out+=['','## Paired hot variants','',
 'Each variant is compared with the baseline from its own counterbalanced cohort; do not treat changes between separate consumer/cohort baselines as a kernel effect.',
 '', '| Cohort / arm | Key bits | Value bytes | Hit | Random 50/50 | Miss | Baseline hit / 50/50 / miss | 50/50 branch misses/op |',
 '| --- | --- | --- | --- | --- | --- | --- | --- |']
for cohort,arms in [('callback',['candidate']),('result',['result','dummy']),('lean',['select','lean']),('carry',['carry'])]:
 for arm in arms:
  for case,w in [('hot32',32),('hot40',40)]:
   for v in [0,8]:
    vals=[ns(cohort,case,v,arm,m) for m in [0,50,100]]
    base=[ns(cohort,case,v,'base',m) for m in [0,50,100]]
    bm=float(get(cohort,case,v,arm,50)['branch_misses'])
    out.append(f'| {cohort}/{arm} | {w} | {v} | '+' | '.join(f'{x:.3f}' for x in vals)+' | '+' / '.join(f'{x:.3f}' for x in base)+f' | {bm:.5f} |')
out+=['','## Final mixed-read counters','',
 '| Key bits | Value bytes | Arm | ns | Cycles/op | Instructions/op | Branches/op | Branch misses/op |',
 '| --- | --- | --- | --- | --- | --- | --- | --- |']
for case,w in [('hot32',32),('hot40',40)]:
 for v in [0,8]:
  for arm in ['base','carry','boost','abseil']:
   r=get('carry',case,v,arm,50)
   out.append(f'| {w} | {v} | {arm} | '+' | '.join(f'{float(r[k]):.5f}' for k in ['ns','cpu_cycles','retired_instructions','branches','branch_misses'])+' |')
out+=['','## Owned bytes','',
 'Same table inputs/capacities as the earlier memory study, selected by bytes rather than winning timing. Static code/constants are outside all table-owned allocation counts. Carry uses one shared eight-byte zero word per concrete type. W50 map8 retains the earlier 25% smaller-than-Boost reference and is not exactly memory-equal.',
 '', '| Case | Value bytes | Ikea | Boost | Abseil |', '| --- | --- | --- | --- | --- |']
for case in ['hot32','hot40','cold40','cold50']:
 for v in [0,8]:
  out.append(f'| {case} | {v} | '+' | '.join(str(int(float(get('carry',case,v,arm,0)['bytes']))) for arm in ['carry','boost','abseil'])+' |')
(a.root/'tables.md').write_text('\n'.join(out)+'\n')
(a.root/'audit.json').write_text(json.dumps(dict(cohorts=audits,processes=sum(v['processes'] for v in audits.values()),read_intervals=sum(v['read_intervals'] for v in audits.values()),cross_cohort_answer_identities=len(answers),same_answers_across_callback_and_result_contracts=True),indent=2)+'\n')
print(a.root/'tables.md')
