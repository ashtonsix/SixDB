#!/usr/bin/env python3
"""Keep every complete Frame point-getter policy cell, including losses."""
from pathlib import Path
import csv,hashlib,json,statistics,sys
from collections import defaultdict
p=Path(sys.argv[1]);groups=defaultdict(list);checks={};skips=[];traces=[]
for f in sorted(p.glob('frame-p*.jsonl')):
 for line in f.read_text().splitlines():
  r=json.loads(line)
  if 'skip' in r:skips.append(r);continue
  if 'trace' in r:traces.append(r);continue
  if 'check_cases' in r:checks[f.name]=r['check_cases'];continue
  key=tuple(r[k] for k in ('runs','bits','blocks','chains','arm'))
  groups[key].append(r['elapsed_ns']/r['requests'])
medians={k:statistics.median(v) for k,v in groups.items()}
with (p/'frame-summary.csv').open('w') as out:
 w=csv.writer(out);w.writerow(['runs','bits','blocks','chains','arm','samples','min_ns','median_ns','max_ns','native_over_projection'])
 for key,v in sorted(groups.items()):
  base=(*key[:-1],key[-1].replace('_native','_projection'))
  ratio=medians[key]/medians[base] if key[-1].endswith('_native') else ''
  w.writerow([*key,len(v),min(v),medians[key],max(v),ratio])
(p/'frame-cases.json').write_text(json.dumps({'checks':checks,'skips':skips,'traces':traces},indent=2)+'\n')
(p/'binary-sha256.json').write_text(json.dumps({n:hashlib.sha256((p/n).read_bytes()).hexdigest() for n in ['intervals_frame_runs','intervals_directory_reads','intervals_consumer_seams','directory_before'] if (p/n).exists()},indent=2)+'\n')
print(len(groups),'Frame policy cells; all raw repetitions retained')
