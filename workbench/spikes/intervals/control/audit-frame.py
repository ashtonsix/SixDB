#!/usr/bin/env python3
"""Select actual linked Frame getter/request bodies without erasing their costs."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('assembly',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
selected={};active=None
for line in a.assembly.read_text().splitlines():
 h=re.match(r'^([0-9a-f]+) <(.*)>:$',line)
 if h:
  active=None;name=h[2]
  if re.match(r'frame_points::(point_reader|compact_reader)(_native_runs)?::get\(',name) or re.match(r'frame_points::(point_reader|compact_reader)_program<.*>::get\(',name) or re.match(r'unsigned long read<(1|8)u, frame_points::(point_reader|compact_reader)',name):
   active=[];selected[name]={'address':h[1],'assembly':active}
 elif active is not None:active.append(line)
assert selected,'no actual getter/request bodies recognized'
a.output.with_suffix('.s').write_text('\n\n'.join(name+'\n'+'\n'.join(s['assembly']) for name,s in selected.items()))
receipt={}
for name,s in selected.items():
 instructions=[line for line in s['assembly'] if re.match(r'\s*[0-9a-f]+:',line)]
 receipt[name]={'address':s['address'],'instructions':len(instructions),
  'calls':[line.strip() for line in instructions if re.search(r'\t(bl|blr|callq?)\s',line)],
  'stack':[line.strip() for line in instructions if re.search(r'(sub\s+sp|subq.*%rsp|stp.*sp.*!)',line)]}
a.output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(len(receipt),'selected actual getter/request bodies')
