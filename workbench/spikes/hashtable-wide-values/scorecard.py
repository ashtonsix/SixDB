#!/usr/bin/env python3
"""Rebuild the scorecard from retained individual repetitions (no native work)."""
import csv
import json
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
rows = []
for cohort in ['main','window','density']+(['staged'] if (root/'staged.csv').exists() else []):
    rows.extend(csv.DictReader((root/(cohort+'.csv')).open()))
fields=['cohort','impl','w','fixture_b','v','population','temperature','hint','prefetch','pipeline','loom','staged','operation','pattern','consumed']
groups={}
for r in rows:
    groups.setdefault(tuple(r[f] for f in fields),[]).append(r)
summary=[]
for key, rr in sorted(groups.items()):
    r=dict(zip(fields,key))
    r.update(samples=len(rr), seeds=len({x['seed'] for x in rr}), ns=statistics.median(float(x['ns']) for x in rr),
             min_ns=min(float(x['ns']) for x in rr), max_ns=max(float(x['ns']) for x in rr),
             bytes=statistics.median(int(x['bytes']) for x in rr),
             peak_bytes=statistics.median(int(x['peak_bytes']) for x in rr),
             source_bytes=statistics.median(int(x['source_bytes']) for x in rr),
             jobs=';'.join(sorted({x['job'] for x in rr})))
    summary.append(r)
with (root/'summary.csv').open('w') as out:
    writer=csv.DictWriter(out,fieldnames=list(summary[0])); writer.writeheader(); writer.writerows(summary)

def select(**kwargs):
    return [r for r in summary if all(str(r[k])==str(v) for k,v in kwargs.items())]

def one(**kwargs):
    r=select(**kwargs)
    assert len(r)==1, (kwargs,len(r))
    return r[0]

packed={'materialized','borrow','slots','slots32'}
labels={'materialized':'Ikea getter','borrow':'Borrowed inline','slots':'Ikea ID8','slots32':'Ikea ID4',
        'abseil':'Abseil','boost':'Boost','f14':'F14Fast','f14value':'F14Value',
        'abseil_slots':'Abseil ID8','boost_slots':'Boost ID8','f14_slots':'F14Fast ID8'}
arms=list(labels)
lines=['# Native Zen 5 scorecard','',
       'All timings are ns/operation (lower is better), medians over all retained',
       'repetitions and three input seeds. Read rows consume every payload byte',
       'unless marked first16. Memory is requested backing bytes/live entry.',
       'P = predictable, U = unpredictable, Auto = caller-owned automatic selection.',
       'PF32 = table-home prefetch 32 queries ahead. Manual16 and Loom16 delay',
       'payload consumption after prefetching its complete byte window. Peer',
       'controls use P and their native lookup. Loom timings include admission,',
       'yield, drain and reduction, but exclude worker/bank setup and teardown.','']

def table(title,headers,data):
    if not data: return
    lines.extend(['## '+title,'','| '+' | '.join(headers)+' |','| '+' | '.join(['---']*len(headers))+' |'])
    lines.extend('| '+' | '.join(map(str,row))+' |' for row in data); lines.append('')

def readrow(arm,cohort,w,b,v,n,temp,pf=0,pipeline=0,loom=0,hint=None,consumed=None,staged=0):
    common=dict(cohort=cohort,impl=arm,w=w,fixture_b=b,v=v,population=n,temperature=temp,
                hint=hint or ('auto' if arm in packed else 'p'),prefetch=pf,pipeline=pipeline,loom=loom,
                staged=staged,operation='read',consumed=consumed or v)
    return [f"{one(**common,pattern=p)['ns']:.2f}" for p in ['hit','random50','miss','alternating50','runs64']]

for v in [8,16,32,64,128]:
    table(f'Hot W32 / V{v} / 6,963 entries, 85% primary occupancy',
          ['Implementation','Hit','Random50','Miss','Alternating50','Runs64'],
          [[labels[arm],*readrow(arm,'main',32,9,v,6963,'warm')] for arm in arms if arm!='f14_slots'])
table('Small-value reference W32 / V8 / 13,926 entries, 42.5% primary occupancy',
      ['Implementation','Hit','Random50','Miss','Alternating50','Runs64'],
      [[labels[arm]+' '+hint,*readrow(arm,'main',32,11,8,13926,'warm',hint=hint)]
       for arm in ['materialized','borrow','abseil','boost','f14']
       for hint in (['p','u','auto'] if arm in packed else ['p'])])
for w,b,v,n in [(32,17,64,2673868)]:
    table(f'Cold full-window W{w} / V{v} / {n:,} entries',
          ['Implementation / consumer','Hit','Random50','Miss','Alternating50','Runs64'],
          [[labels[arm]+' '+mode,*readrow(arm,'window',w,b,v,n,'cold',pf,pipeline,loom)]
           for arm in arms for mode,pf,pipeline,loom in [('Direct PF32',32,0,0),('Manual16 PF32',32,16,0),('Loom16 PF32',32,0,1)]])
    data=[]
    for arm in arms:
        eligible=select(cohort='window',impl=arm,w=w,v=v,population=n,operation='read',consumed=v)
        cells=[]
        for pattern in ['hit','random50','miss']:
            best=min((r for r in eligible if r['pattern']==pattern),key=lambda r:r['ns'])
            mode='Loom16' if best['loom']=='1' else 'Manual16' if best['pipeline']=='16' else 'Direct'
            cells.append(f"{best['ns']:.2f} ({mode}, PF{best['prefetch']})")
        data.append([labels[arm],*cells])
    table(f'Best measured read treatment per arm W{w} / V{v}', ['Implementation','Hit','Random50','Miss'],data)
for v in [8,16,32,64,128]:
    data=[]
    b=18 if v<=16 else 16 if v==128 else 17
    n=(1<<b)*8*(63//(32-b+1))*85//100
    for arm in arms:
        if arm=='f14_slots' and v not in [32,64,128]: continue
        cohort='window' if arm=='f14_slots' else 'main'
        rr=select(cohort=cohort,impl=arm,w=32,v=v,population=n,temperature='cold')
        ns=[]
        for op in ['replace','increment','insert','upsert50','erase','refill','growth']:
            r=[r for r in rr if r['operation']==op]
            assert len(r)==1,(v,arm,op,len(r))
            ns.append(f"{r[0]['ns']:.2f}")
        data.append([labels[arm],*ns])
    table(f'Cold writes and growth W32 / V{v}', ['Implementation','Replace','Increment8','Insert','Upsert50','Erase','Refill','Grow/entry'],data)
for temp,b,n in [('warm',9,6963),('cold',17,2673868)]:
    data=[]
    for arm in arms:
        cohort='window' if arm=='f14_slots' else 'main'
        if arm=='f14_slots' and temp=='warm': continue
        r=one(cohort=cohort,impl=arm,w=32,v=64,population=n,temperature=temp,operation='memory')
        g=one(cohort=cohort,impl=arm,w=32,v=64,population=n,temperature=temp,operation='growth')
        data.append([labels[arm],f"{r['bytes']/n:.2f}",f"{g['ns']:.2f}"])
    table(f'V64 {temp} memory and growth (Ikea retains source; peer native rehash releases it)',
          ['Implementation','Steady B/live','Grow ns/live'],data)
for temp,b,n in [('warm',9,6963),('cold',17,2673868)]:
    data=[]
    for arm in arms:
        for index_b in ([b,b+1] if arm in ['slots','slots32'] else [b]):
            modes=[('Direct',0,0,0)] if temp=='warm' else []
            for mode,pf,pipe,loom in modes:
                rr=readrow(arm,'density',32,index_b,64,n,temp,pf,pipe,loom)
                mem=one(cohort='density',impl=arm,w=32,fixture_b=index_b,v=64,population=n,temperature=temp,operation='memory')
                data.append([labels[arm]+f' b{index_b} '+mode,*rr[:3],f"{mem['bytes']/n:.2f}"])
    table(f'Fixed-population index-capacity ablation V64 {temp}', ['Implementation / index b / consumer','Hit','Random50','Miss','B/live'],data)
    table(f'Index-capacity ablation V64 {temp}: writes and growth',
          ['Implementation','Replace','Increment8','Insert','Upsert50','Erase','Refill','Grow/entry'],
          [[labels[arm]+f' b{index_b}', *[f"{one(cohort='density',impl=arm,w=32,fixture_b=index_b,v=64,population=n,temperature=temp,operation=op)['ns']:.2f}"
                 for op in ['replace','increment','insert','upsert50','erase','refill','growth']]]
           for arm in arms for index_b in ([b,b+1] if arm in ['slots','slots32'] else [b])])
# The projected consumer checks the important directory-like two-limb case.
table('Cold W32 / V64 / first16 consumed, PF32',
      ['Implementation / consumer','Hit','Random50','Miss','Alternating50','Runs64'],
      [[labels[arm]+' '+mode,*readrow(arm,'window',32,17,64,2673868,'cold',32,pipe,loom,consumed=16)]
       for arm in arms for mode,pipe,loom in [('Direct',0,0),('Manual16',16,0),('Loom16',0,1)]])
if (root/'staged.csv').exists():
    stage_start=len(lines)
    for w,b,v,n in [(32,17,32,2673868),(32,17,64,2673868),(32,16,128,1336934),(40,17,64,1782579),(32,18,64,2673868)]:
        data=[]
        eligible_arms=['slots','slots32'] if b==18 else arms
        for arm in eligible_arms:
            modes=[('Direct PF0',0,0,0,0),('Direct PF32',32,0,0,0),('Payload-only Manual16',32,16,0,0),('Payload-only Loom16',32,0,1,0)]
            if arm in ['slots','slots32','abseil_slots','boost_slots','f14_slots']:
                modes += [('ID + payload Manual16',32,16,0,1),('ID + payload Loom16',32,0,1,1)]
            for mode,pf,pipe,loom,staged in modes:
                data.append([labels[arm]+' '+mode,*readrow(arm,'staged',w,b,v,n,'cold',pf,pipe,loom,staged=staged)])
        table(f'Proper ID-before-payload staging W{w} / b{b} / V{v} / {n:,} entries',
              ['Implementation / consumer','Hit','Random50','Miss','Alternating50','Runs64'],data)
    first_table=next(i for i,line in enumerate(lines) if line.startswith('## '))
    lines=lines[:first_table]+lines[stage_start:]+lines[first_table:stage_start]
(root/'tables.md').write_text('\n'.join(lines)+'\n')
(root/'coverage.json').write_text(json.dumps({'retained_rows':len(rows),'summary_rows':len(summary),
        'three_seed_cases':sum(r['seeds']==3 for r in summary)},indent=2)+'\n')
print(json.dumps({'rows':len(rows),'summary_rows':len(summary),'table_lines':len(lines)},indent=2))
