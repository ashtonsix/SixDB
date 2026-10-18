#!/usr/bin/env python3
"""Summarize native tile consumers. Keep order, metrics and geometry separate."""
import argparse, csv, hashlib, json, pathlib, statistics
from collections import defaultdict
p=argparse.ArgumentParser()
p.add_argument('group',type=pathlib.Path)
p.add_argument('output',type=pathlib.Path)
a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=True)
g=json.loads(a.group.read_text()); samples=defaultdict(list); sources=[]
for machine,m in g['members'].items():
    job=m.get('job_id') or m.get('job')
    if isinstance(job,dict): job=job['id']
    if not job:
        # Group records its launch identity separately on older receipts.
        job=m['id']
    root=pathlib.Path('build/workers')/job
    status=json.loads((root/'status.json').read_text())
    for path in sorted((root/'results').glob('*.csv')):
        profile=machine+'-'+path.stem.split('-')[0]
        order='reverse' if 'reverse' in path.stem else 'forward'
        for r in csv.DictReader(path.open()):
            key=(profile,order)+tuple(int(r[x]) for x in ['width','count','op','offset','stride','active','arm'])+(r['metric'],)
            samples[key].append(float(r['ns']))
        sources.append(dict(profile=profile,order=order,job=job,file=path.name,
                            sha256=hashlib.sha256(path.read_bytes()).hexdigest(),artifact=status.get('artifact')))
fields=['profile','order','width','count','op','offset','stride','active','arm','metric']
with (a.output/'medians.csv').open('w') as f:
    w=csv.writer(f); w.writerow(fields+['median_ns','min_ns','max_ns','repetitions'])
    for k,v in sorted(samples.items()): w.writerow(list(k)+[statistics.median(v),min(v),max(v),len(v)])
(a.output/'sources.json').write_text(json.dumps(sources,indent=2)+'\n')
print(f'{len(samples)} independent metric/order cells; {sum(map(len,samples.values()))} observations')
