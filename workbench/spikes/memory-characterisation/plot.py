#!/usr/bin/env python3
"""Plot selected retained curves; requires Matplotlib (rendered with 3.11.2)."""
import csv
import json
import statistics as stats
from collections import defaultdict
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from analyze import analyse

ROOT = Path(__file__).resolve().parent
plt.rcParams.update({'font.size': 9, 'axes.spines.top': False, 'axes.spines.right': False,
                     'axes.titleweight': 'bold', 'svg.fonttype': 'none'})
fig, axes = plt.subplots(2, 3, figsize=(15, 8), layout='constrained')
fig.suptitle('Memory characterisation • observed behaviour', fontsize=17, y=.985)
sets = [(['c5a.xlarge','c6a.xlarge','c7a.large','c8a.large'], ['Zen 2','Zen 3','Zen 4','Zen 5'], 'AMD'),
        (['c6i.xlarge','c7i.xlarge','c8i.xlarge'], ['Ice Lake','Sapphire Rapids','Granite Rapids'], 'Intel'),
        (['c6g.large','c7g.large','c8g.large'], ['Graviton 2','Graviton 3','Graviton 4'], 'Arm')]
for ax, (names, labels, title) in zip(axes[0], sets):
    for name, label in zip(names, labels):
        data = analyse(ROOT / 'evidence' / name / 'screen', write=False)
        point = next(p for p in data['mlp'] if p['condition']=='isolated' and p['bytes']==268435456)
        ax.plot([p['k'] for p in point['curve']], [p['ns_per_load'] for p in point['curve']], '.-', label=label)
    ax.set(xscale='log', yscale='log', title=f'{title}: independent random chains',
           xlabel='Independent chains K', ylabel='ns / demand load', ylim=(4, 210), xticks=[1,4,16,64])
    ax.set_xticklabels(['1','4','16','64'])
    ax.grid(alpha=.2); ax.legend(frameon=False)
ax = axes[1,0]
for machine, colour in [('zen5','#245fce'),('granite-rapids','#cf681b')]:
    groups = defaultdict(list)
    with (ROOT / 'evidence/streams' / machine / 'streams.csv').open() as stream:
        for row in csv.DictReader(stream):
            groups[row['variant'],int(row['k'])].append(float(row['median']))
    for variant, style in [('stride2','-'),('shuffled-within-page','--')]:
        points = sorted((k,stats.median(v)) for (name,k),v in groups.items() if name==variant)
        ax.plot([p[0] for p in points],[p[1] for p in points],style,color=colour,
                label=f'{machine}: {"ordered" if variant=="stride2" else "shuffled"}')
ax.set(title='Interleaved streams, one dependent chain', xlabel='Streams (one page each)', ylabel='ns / load')
ax.legend(fontsize=8,frameon=False); ax.grid(alpha=.2)
ax = axes[1,1]
rows=list(csv.DictReader((ROOT/'evidence/c8a.24xlarge/screen/samples.csv').open()))
cases=[('Same LLC\npeer clean','separate-core-cpu1-handoff','peer-clean'),
       ('Other LLC\npeer clean','separate-core-cpu8-handoff','peer-clean'),
       ('Other LLC\npeer dirty','separate-core-cpu8-handoff','peer-dirty'),
       ('Cold\ncontrol','separate-core-cpu8-handoff','cold')]
values=[stats.median(float(r['median']) for r in rows if r['condition']==condition and r['variant']==state and r['family']=='handoff')
        for _,condition,state in cases]
ax.bar([c[0] for c in cases],values,color=['#245fce','#7b57b5','#c35b92','#9eabb8'],width=.65)
for i,value in enumerate(values): ax.text(i,value+4,f'{value:.0f}',ha='center')
ax.set(title='Zen 5: peer-cached can cost more than cold',ylabel='Timed receiver load (ns)',ylim=(0,330))
ax = axes[1,2]
data=analyse(ROOT/'evidence/c8i.xlarge/screen',write=False)
conditions=['isolated','separate-core-cpu1-stream','smt-sibling-cpu2-stream']
values=[next(p['best_ns_per_load'] for p in data['mlp'] if p['condition']==condition and p['bytes']==268435456) for condition in conditions]
ax.bar(['Isolated','Other core\nstreaming','SMT sibling\nstreaming'],values,color=['#245fce','#6f8cb3','#c35b92'],width=.65)
for i,value in enumerate(values): ax.text(i,value+.25,f'{value:.1f}',ha='center')
ax.set(title='Granite Rapids: SMT interference',ylabel='Best ns / demand load',ylim=(0,max(values)*1.2))
fig.get_layout_engine().set(rect=(0,.045,1,.88))
fig.text(.01,.008,'2026-09-13 · Clang 21.1.8 · 256 MiB, base pages · CPU pinned · medians across repetitions · sources and qualifications in METHOD.md / FLEET.md',fontsize=9)
fig.savefig(ROOT/'measurements.png',dpi=150)
fig.savefig(ROOT/'measurements.svg')
print(f'Plotted with Matplotlib {matplotlib.__version__}')
