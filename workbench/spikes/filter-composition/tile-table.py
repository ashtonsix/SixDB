#!/usr/bin/env python3
"""Readable native8 extract; full samples and other placements remain recoverable."""
import argparse,csv,collections,json,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('group',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
data=collections.defaultdict(list)
g=json.loads(a.group.read_text())
for machine,member in g['members'].items():
    job=member['job']
    for path in (pathlib.Path('build/workers')/job/'results').glob('*.csv'):
        profile=machine+'-'+path.stem.split('-')[0]
        for r in csv.DictReader(path.open()):
            if r['count']=='8' and r['offset']=='1' and r['active']=='255':
                data[profile,int(r['width']),int(r['op']),int(r['arm']),r['metric']].append(float(r['ns']))
med={k:statistics.median(v) for k,v in data.items()}
profiles=['zen5-avx2','zen5-avx512','granite-avx2','granite-avx512','neon-neon']
names=['Zen 5 AVX2','Zen 5 AVX-512','Granite Rapids AVX2','Granite Rapids AVX-512','Neoverse V2 NEON']
ops=['==','<','<=','>','>=']
with (a.output/'seriespack-tiles.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['profile','K','predicate','current_latency_ns','current_reciprocal_ns','fused_latency_ns','fused_reciprocal_ns','scalar_tail_latency_ns','scalar_tail_reciprocal_ns'])
    for profile in profiles:
        for k in range(1,65):
            for op in range(5):
                w.writerow([profile,k,ops[op]]+[med[profile,k,op,arm,metric] for arm in (0,2,1) for metric in ('latency','throughput')])
lines=['# Native eight-slot SeriesPack filters','',
       'Equality, full activity, Local H=0, payload origin at byte offset1. **Nanoseconds per eight-slot window**; lower is better. Each pair is **dependent latency / reciprocal throughput**. The throughput consumer has eight independently salted result-dependent streams. The [CSV](seriespack-tiles.csv) includes all five predicates and the scalar-tail control.', '',
       'Medians pool five forward-order and five reverse-order repetitions, 40,000 iterations each. These are complete warmed mask-to-address consumers on finite 64-window working sets; query loads and address recurrence are included. They are not DRAM, Loom or hash-table operation measurements. Hundredths are display precision. [Qualification](TILE-FILTERS.md) owns controls, selection, variability and provenance.', '']
for profile,name in zip(profiles,names):
    lines += ['## '+name,'','| K | SeriesPack latency / reciprocal ns | Independent fused latency / reciprocal ns |','|---:|---:|---:|']
    for k in range(1,65):
        v=[med[profile,k,0,arm,metric] for arm in (0,2) for metric in ('latency','throughput')]
        lines.append(f'| {k} | {v[0]:.2f} / {v[1]:.2f} | {v[2]:.2f} / {v[3]:.2f} |')
    lines.append('')
(a.output/'seriespack-tiles.md').write_text('\n'.join(lines))
print('native8 extract: 1,600 predicate/profile/width pairs')
