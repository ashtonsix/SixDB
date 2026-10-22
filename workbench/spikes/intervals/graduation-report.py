#!/usr/bin/env python3
"""Unpooled before/after median ratios for every retained native cell.

Input is a worker results root containing PROFILE/{before,after}/summary.csv.
Ratios are descriptive medians, not confidence intervals or paired inner samples.
"""
import argparse,csv
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('results',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
keys=['panel','operation','maximum','count','chains','clustered','arm'];rows=[]
for baseline in sorted(args.results.glob('*/before/summary.csv')):
    after=baseline.parent.parent/'after/summary.csv'
    a={tuple(r[k] for k in keys):r for r in csv.DictReader(baseline.open())}
    b={tuple(r[k] for k in keys):r for r in csv.DictReader(after.open())}
    if a.keys()!=b.keys():raise ValueError('cell set differs: '+str(baseline))
    for key,old in a.items():
        new=b[key]
        rows.append({'profile':baseline.parent.parent.name,**dict(zip(keys,key)),
          'before_ns':old['median_ns'],'after_ns':new['median_ns'],
          'ratio':float(new['median_ns'])/float(old['median_ns']),
          'before_samples':old['samples'],'after_samples':new['samples']})
if not rows:raise ValueError('no before/after profiles')
args.output.parent.mkdir(parents=True,exist_ok=True)
with args.output.open('w') as out:
    writer=csv.DictWriter(out,fieldnames=list(rows[0]),lineterminator='\n');writer.writeheader();writer.writerows(rows)
print(len(rows),'comparison cells; all losses retained')
