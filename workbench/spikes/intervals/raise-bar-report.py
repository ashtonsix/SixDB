#!/usr/bin/env python3
"""Keep baseline/current/context and all losing complete-operation cells."""
import csv,json,statistics,sys
from collections import defaultdict
from pathlib import Path
p=Path(sys.argv[1]);groups=defaultdict(list);metadata={}
for f in p.glob('*.jsonl'):
    for line in f.read_text().splitlines():
        r=json.loads(line)
        panel='chains' if 'chains' in r else 'geometry' if 'maximum' in r else 'edits' if 'effect_bytes' in r else 'seams'
        key=(panel,r.get('case',r.get('operation','')),r.get('maximum',''),r.get('count',''),r.get('chains',''),r.get('clustered',''),r['arm'])
        groups[key].append(r['elapsed_ns']/r['operations'])
        metadata[key]={k:r[k] for k in ['data_bytes','control_bytes','capacity_bytes','owner_bytes','binding_bytes','field_bytes','checkpoint_bytes','effect_bytes','operations'] if k in r}
with (p/'summary.csv').open('w') as out:
    w=csv.writer(out,lineterminator='\n');w.writerow(['panel','operation','maximum','count','chains','clustered','arm','samples','min_ns','median_ns','max_ns'])
    for key,x in sorted(groups.items(),key=lambda kv:str(kv[0])):w.writerow([*key,len(x),min(x),statistics.median(x),max(x)])
(p/'metadata.json').write_text(json.dumps([dict(key=list(k),**v) for k,v in metadata.items()],indent=2)+'\n')
print(len(groups),'cells;',sum(map(len,groups.values())),'raw samples; all losses retained')
