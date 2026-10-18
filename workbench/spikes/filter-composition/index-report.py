#!/usr/bin/env python3
"""Keep first-match results separate from all-match filter measurements."""
import argparse, collections, csv, hashlib, json, pathlib, statistics
p=argparse.ArgumentParser(); p.add_argument('group',type=pathlib.Path); p.add_argument('out',type=pathlib.Path)
a=p.parse_args(); a.out.mkdir(parents=True,exist_ok=True)
g=json.loads(a.group.read_text()); data=collections.defaultdict(list); sources=[]; members={}
for machine,member in g['members'].items():
    job=member['job']; root=pathlib.Path('build/workers')/job
    status=json.loads((root/'status.json').read_text()); assert status['script_returncode']==0
    spec=json.loads((root/'job.json').read_text())
    members[machine]={'job':job,'artifact':status['artifact'],'source':spec['source'],
        'regression':{p.name:p.read_text().strip() for p in (root/'results').glob('*-regression.txt')}}
    for path in (root/'results').glob('*-index-*.csv'):
        arch,_,order=path.stem.split('-'); profile=machine+'-'+arch
        for r in csv.DictReader(path.open()):
            key=(profile,)+tuple(int(r[x]) for x in ['width','count','pattern','active','arm'])+(r['metric'],)
            data[key].append((int(order),float(r['ns'])))
        sources.append({'job':job,'file':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
fields=['profile','width','count','pattern','active','arm','metric']
with (a.out/'indexof.csv').open('w') as f:
    w=csv.writer(f);w.writerow(fields+['median_ns','forward_ns','reverse_ns','min_ns','max_ns','samples'])
    for key,rows in sorted(data.items()):
        values=[v for _,v in rows]; assert len(values)==10,(key,len(values))
        w.writerow(list(key)+[statistics.median(values)]+[statistics.median(v for o,v in rows if o==i) for i in (0,1)]+[min(values),max(values),len(values)])
(a.out/'index-provenance.json').write_text(json.dumps({'group':g['id'],'members':members,'index_sources':sources},indent=2)+'\n')
print(f'{len(data)} first-match cells; {sum(len(v) for v in data.values())} observations')
