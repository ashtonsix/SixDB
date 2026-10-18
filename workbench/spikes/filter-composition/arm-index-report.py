#!/usr/bin/env python3
"""Summarize matched ARM index consumers; do not label predictable misses latency."""
import argparse,csv,json,pathlib,statistics,hashlib
parser=argparse.ArgumentParser()
parser.add_argument('files',nargs='+',type=pathlib.Path)
parser.add_argument('--output',required=True,type=pathlib.Path)
args=parser.parse_args()
groups={}
for path in args.files:
    for row in csv.DictReader(path.open()):
        key=tuple(int(row[x]) for x in ('width','count','pattern','active','arm'))+(row['metric'],)
        groups.setdefault(key,[]).append(float(row['ns']))
args.output.mkdir(parents=True,exist_ok=True)
with (args.output/'summary.csv').open('w') as f:
    out=csv.writer(f);out.writerow(['width','count','pattern','active','arm','metric','samples','median_ns','min_ns','max_ns'])
    for key,values in sorted(groups.items()):
        out.writerow([*key,len(values),statistics.median(values),min(values),max(values)])
arms={0:'Previous algorithm',3:'Fused',5:'Previous body-first',7:'Bounded body-first',8:'Owner body-first',9:'Bounded full',10:'Owner full',11:'Ikea indexof',12:'Ikea full filter + first bit'}
panels=[(8,'Random uniform hits: dependent latency / reciprocal throughput, ns per query'),
        (13,'Shared-prefix late hits: dependent latency / reciprocal throughput, ns per query'),
        (9,'Random misses: one-stream cost / reciprocal throughput, ns per query')]
lines=[]
present=[a for a in arms if any(k[4]==a for k in groups)]
for pattern,title in panels:
    lines += ['## '+title,'','| K / N | '+' | '.join(arms[a] for a in present)+' |','| --- |'+' --- |'*len(present)]
    for k,n in sorted({key[:2] for key in groups}):
        active=(1<<n)-1
        if (k,n,pattern,active,present[0],'serial') not in groups:continue
        cells=[]
        for arm in present:
            if (arm in (7,8,9,10) and k%8<4) or (arm==5 and not(k>8 and k%8)):
                cells.append('—')
                continue
            vals=[]
            for metric in ('serial','throughput'):
                samples=groups.get((k,n,pattern,active,arm,metric))
                vals.append(f'{statistics.median(samples):.2f}' if samples else '—')
            cells.append(' / '.join(vals))
        lines += [f'| {k} / {n} | '+' | '.join(cells)+' |']
    lines += ['']
(args.output/'table.md').write_text('\n'.join(lines))
(args.output/'inputs.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in args.files},indent=2)+'\n')
print('\n'.join(lines[:25]))
