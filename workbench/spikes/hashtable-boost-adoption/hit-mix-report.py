#!/usr/bin/env python3
"""Render bounded branch/order and overflow-speculation comparisons."""
import argparse
import csv
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('root',type=Path)
p.add_argument('--speculation',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
rows=list(csv.DictReader((a.root/'summary.csv').open()))
index={(int(r['w']),int(r['v']),r['impl'],int(r['miss']),int(r['order']),int(r['sink'])):r for r in rows}
out=['# Hot mixed-hit diagnosis', '',
     'Zen 5 AVX-512, fixed memory-matched whole-table geometry, n=13,926. Medians',
     'across three seeds, two fresh processes/seed, three one-million-request',
     'intervals/process. Every PMU group has 100% running coverage and is unscaled.',
     'All order controls retain the same query multiset and within-class order.', '',
     '## Full consumer: time and branch misses', '',
     'Each cell is **ns/lookup / retired branch misses per lookup**. Lower is better.',
     'Full consumers count hits and hit keys; maps also consume the complete value.', '',
     '| W / value | Table | Random 50% | Alternating 50% | Runs of 64 | Grouped 50% | All hit | All miss |',
     '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
for w,v in [(32,0),(32,8),(40,0),(40,8)]:
    for impl in ['ikea','boost','abseil']:
        rr=[index[(w,v,impl,miss,order,0)] for miss,order in [(50,0),(50,1),(50,2),(50,3),(0,0),(100,0)]]
        cells=[f"{float(r['ns']):.3f} / {float(r['branch_misses']):.4f}" for r in rr]
        out.append(f"| {w} / {'set' if not v else 'map8'} | {impl} | {' | '.join(cells)} |")
out.extend(['', '## Membership-only consumer', '',
     'Each cell is **ns/lookup / retired branch misses per lookup**. No hit-key',
     'sum or mapped-value consumption occurs in this loop.', '',
     '| W / value | Table | Random 50% | Alternating 50% | Grouped 50% |',
     '| --- | --- | ---: | ---: | ---: |'])
for w,v in [(32,0),(32,8),(40,0),(40,8)]:
    for impl in ['ikea','boost','abseil']:
        rr=[index[(w,v,impl,50,order,1)] for order in [0,1,3]]
        cells=[f"{float(r['ns']):.3f} / {float(r['branch_misses']):.4f}" for r in rr]
        out.append(f"| {w} / {'set' if not v else 'map8'} | {impl} | {' | '.join(cells)} |")
out.extend(['', '## Instruction work versus predictor loss', '',
     'Full consumer; **cycles / retired instructions / branch misses**, per lookup.',
     'These are observed loop counts, not a raw architectural misprediction penalty.', '',
     '| W / value | Table | Random 50% | Alternating 50% |',
     '| --- | --- | ---: | ---: |'])
for w,v in [(32,0),(32,8),(40,0),(40,8)]:
    for impl in ['ikea','boost','abseil']:
        cells=[]
        for order in [0,1]:
            r=index[(w,v,impl,50,order,0)]
            cells.append(f"{float(r['cpu_cycles']):.2f} / {float(r['retired_instructions']):.2f} / {float(r['branch_misses']):.4f}")
        out.append(f"| {w} / {'set' if not v else 'map8'} | {impl} | {' | '.join(cells)} |")
if a.speculation:
    rr=list(csv.DictReader((a.speculation/'summary.csv').open()))
    ix={(int(r['w']),int(r['v']),r['impl'],int(r['order']),int(r['sink'])):r for r in rr}
    out.extend(['', '## Paired barriers at overflow-helper entry', '',
        'Each cell is **ns/lookup / branch misses per lookup**. CPUID is included',
        'only for W40/set, where every process has zero real spills and therefore',
        'no required overflow-helper call. This avoids paying its large retired cost.',
        'The helper barrier does not prevent all speculative call/stack/front-end',
        'work before entry; it blocks the subsequent Swiss probe execution.', '',
        '| W / value | Sink | Order | Baseline | LFENCE | CPUID |',
        '| --- | --- | --- | ---: | ---: | ---: |'])
    for w,v in [(32,0),(32,8),(40,0),(40,8)]:
        for sink in [0,1]:
            for order in [0,1,3]:
                cells=[]
                for variant in ['base','lfence','cpuid']:
                    r=ix.get((w,v,variant,order,sink))
                    cells.append(f"{float(r['ns']):.3f} / {float(r['branch_misses']):.4f}" if r else '—')
                out.append(f"| {w} / {'set' if not v else 'map8'} | {'hits' if sink else 'full'} | "
                           f"{['random','alternating','runs64','grouped'][order]} | {' | '.join(cells)} |")
a.output.write_text('\n'.join(out)+'\n')
print(a.output)
