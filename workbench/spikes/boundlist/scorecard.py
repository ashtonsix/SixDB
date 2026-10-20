#!/usr/bin/env python3
"""Readable slices of the retained production matrices; no timing subtraction."""
import csv,pathlib,sys
p=pathlib.Path(sys.argv[1]);out=[]
profiles=[('zen5','avx2','Zen 5 AVX2'),('zen5','avx512','Zen 5 AVX-512'),('granite','avx2','Granite AVX2'),('granite','avx512','Granite AVX-512'),('neon','neon','Neoverse V2 NEON')]
cache={}
def value(study,host,isa,**terms):
    if study not in cache:cache[study]=list(csv.DictReader((p/f'{study}.csv').open()))
    rows=cache[study]
    rows=[r for r in rows if r['host']==host and r['isa']==isa and all(r[k]==str(v) for k,v in terms.items())]
    assert len(rows)==1,(study,host,isa,terms,len(rows))
    return f"{float(rows[0]['median_ns']):.2f}"
def table(title,columns,rows):
    out.extend([f'## {title}','', '| '+' | '.join(columns)+' |','| '+' | '.join(['---']+['---:']*(len(columns)-1))+' |'])
    out.extend('| '+' | '.join(map(str,r))+' |' for r in rows);out.append('')
out.extend(['# Boundlist production scorecard','',
'All times are **median ns/request**. Serial is one result-dependent chain;',
'8-way/16-way columns are reciprocal throughput per request, not batch latency.',
'Hot-list tables include query preparation and dependent child consumption.',
'Setup and memory are separate; full min/max/repetitions and losing cells remain in the CSV matrices.',''])
for bits,n in [(32,4096),(64,16384),(32,256)]:
    rows=[]
    for h,i,label in profiles:
        def q(impl,ch):return value('search',h,i,bits=bits,count=n,distribution=0,implementation=impl,metric='complete_query_child_hash',chains=ch)
        rows.append([label,q('production_select_window',1),q('production_bound_search',1),q('production_batch',8),q('production_bound_batch',8)])
    table(f'Complete point consumer: {n:,} u{bits} endpoints, positive deltas',
          ['Host / ISA','Ordinary serial','Bound serial','Ordinary batch8','Bound batch8'],rows)
rows=[]
for h,i,label in profiles:
    def q(impl,metric):return value('ordered',h,i,bits=32,count=4096,nodes=1,repeat=8,gap=1,span=257,sparse=1,implementation=impl,metric=metric)
    for impl,name in [('production_boundlist','Ordinary'),('production_bound_search','Bound'),('independent_fused_binary','Fused binary')]:
        rows.append([label+' · '+name,q(impl,'dependent_extrema_children'),q(impl,'independent8_extrema_children')])
table('Complete occupied-extrema range: 4,096 u32 ranks, duplicates and population',
      ['Host / implementation','Serial','8-way'],rows)
rows=[]
for h,i,label in profiles:
    def q(impl,metric):return value('promotion',h,i,layout='width',maximum=4095,count=256,nodes=1,range_span=257,implementation=impl,metric=metric)
    rows.append([label,q('before_reverse','dependent_range_child'),q('production_reverse','dependent_range_child'),q('before_reverse','independent8_range_child'),q('production_reverse','independent8_range_child')])
table('Complete physical range: W16 sidecar, 256 ranks',
      ['Host / ISA','Before serial','Adopted serial','Before 8-way','Adopted 8-way'],rows)
for mixed,name in [(0,'full nodes'),(1,'mixed 64–256 ranks')]:
    rows=[]
    for h,i,label in profiles:
        def q(impl,e,w):return value('cold_adoption',h,i,nodes=524288,mixed=mixed,span=257,implementation=impl,execution=e,width=w,metric='range_children')
        rows.append([label,q('width_checkpoint_window','direct',1),q('production_sidecar_phase','direct',1),q('width_checkpoint_window','loom_hints',16),q('production_sidecar_phase','loom_hints',16)])
    table(f'Large random range consumer: {name}',
          ['Host / ISA','Raw serial','Typed serial','Raw Loom16','Typed Loom16'],rows)
out.extend(['Both controls use 64B owner frames. The active Width operand bound is 288MiB;',
'Granite reports 480MiB L3, so this is not a proven above-LLC/DRAM table.',
'The fixed-node typed phase has a measurable throughput cost against the raw',
'geometry control; the mixed-node phase can avoid absent-read work. Hints are',
'line-covered and the scheduler is the actual Loom Ring.',''])
rows=[]
for h,i,label in profiles:
    def q(impl,ch):return value('upper',h,i,bits=32,count=4096,run=80,chains=ch,implementation=impl)
    rows.append([label,q('production_select_window',1),q('production_bound_search',1),q('production_batch',8),q('production_bound_batch',8)])
table('Upper-bound counterexample: 4,096 u32 ranks, runs of 80',
      ['Host / ISA','Ordinary serial','Bound serial','Ordinary batch8','Bound batch8'],rows)
out.extend(['This panel uses prepared queries, an 8B selected child and a full 1024-query',
'feedback cycle. It is a separate envelope from the generated-query/hash panel.',
'The heuristic declines interpolation here; binding/batch plumbing can still cost',
'more than a raw source. Use ordinary sources for this regime.',''])
rows=list(csv.DictReader((p/'preparation.csv').open()))
prepared=[]
for h,i,label in profiles:
    r=next(r for r in rows if r['host']==h and r['isa']==i and r['bits']=='32' and r['count']=='4096' and r['distribution']=='0')
    prepared.append([label,r['hot_loop_mean_ns'],r['binding_bytes'],int(r['binding_bytes'])-16])
table('Source preparation: repeated hot loop, N4096/u32/positive deltas',
      ['Host / ISA','Loop-mean ns/bind','Total binding bytes','Extra over raw16B'],
      prepared)
out.extend(['Each setup observation averages 4096 cache-hot constructions, not independent',
'repetitions or cold-source setup. Rebuild after endpoint/count/placement changes;',
'version lifetime determines whether repeated-query savings amortize setup.',
'Encoded sidecar bytes and 16B typed accesses are unchanged. Width phases are 16B',
'(population adds a borrowed-source pointer); numeric query phases are 32B.',''])
(p/'scorecard.md').write_text('\n'.join(out))
