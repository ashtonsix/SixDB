#!/usr/bin/env python3
"""Retain bounded K40/A10 multimap mutation callees; static reachability only."""
import argparse,gzip,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(__doc__);p.add_argument('assembly',type=Path);p.add_argument('binary',type=Path);p.add_argument('audit',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
text=gzip.open(a.assembly,'rt').read();heads=list(re.finditer(r'^([0-9a-f]+) <(.+)>:',text,re.M));funcs={int(m[1],16):(m[2],text[m.end():heads[i+1].start() if i+1<len(heads) else len(text)]) for i,m in enumerate(heads)};del text
pat=re.compile(r'^\s*([0-9a-f]+):\s+(callq?|bl)\s+(0x[0-9a-f]+) <(.+)>$',re.M)
audit=json.loads(a.audit.read_text());result=[];addresses=set()
for r in audit['timed_roots']:
 if (r['K'],r['kind'],r['A'],r['phase'])!=(40,2,10,'update') or r['arm'] not in [0,2,4,6]:continue
 item={k:r[k] for k in ['K','kind','A','arm','clock','end','calls']};item['functions']=[]
 pending=[(int(c['target'].split(' ',1)[0],16),0) for c in r['calls'] if 'try_set<' in c['target'] or 'EmplaceBackSlow<' in c['target']];seen=set()
 while pending:
  address,depth=pending.pop(0)
  if address in seen or address not in funcs:continue
  seen.add(address);name,body=funcs[address];addresses.add(address)
  calls=[dict(address='0x'+m[1],target=m[3],symbol=m[4]) for m in pat.finditer(body)]
  item['functions'].append(dict(address=hex(address),depth=depth,symbol=name,calls=calls))
  (a.output/f'K40-mm-arm{r["arm"]}-callee-{address:x}.asm').write_text(hex(address)+' <'+name+'>:'+body)
  if depth<2:
   for call in calls:
    if any(x in call['symbol'] for x in ['::try_set<','::reserve_values<']):pending.append((int(call['target'],16),depth+1))
 result.append(item)
loc=subprocess.run(['llvm-addr2line-21','-a','-i','-e',str(a.binary)],input='\n'.join(hex(x) for x in sorted(addresses))+'\n',text=True,capture_output=True,check=True)
(a.output/'mutation-callsites.txt').write_text(loc.stdout)
(a.output/'mutation-paths.json').write_text(json.dumps(dict(scope='K40 multimap A10, arms0/2/4/6; static reachable calls, no dynamic branch-frequency or time attribution',regions=result),indent=2)+'\n')
print('Retained',len(result),'mutation regions and',sum(len(x['functions']) for x in result),'bounded callees')
