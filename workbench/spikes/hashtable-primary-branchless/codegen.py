#!/usr/bin/env python3
"""Retain complete native read loops and their conditional branches for review.

Counts describe static code, including rare helpers; they do not assert an
unexecuted branch is costly or prove a data dependency from a mnemonic count.
"""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--trim-separators',action='store_true');p.add_argument('--precise-names',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def sections(path):
 rows=path.read_text().splitlines();out={};start=None;name=None
 for i,line in enumerate(rows):
  if re.match(r'^[0-9a-f]+ <',line):
   if start is not None:out[name]=rows[start:i]
   name=line.split(' <',1)[1].rsplit('>:',1)[0];start=i
 if start is not None:out[name]=rows[start:]
 return out
proof=[]
for path in sorted((a.root/'assembly').glob('*memory_ikea*.asm')):
 for name,body in sections(path).items():
  if not any(x in name for x in ['read_loop<','hint_range<','hint_requests<']):continue
  branches=[];masked=[];selects=[]
  for line in body[1:]:
   m=re.match(r'\s*([0-9a-f]+):\s+(.*)',line)
   if not m:continue
   ins=m[2];op=ins.split()[0]
   if op.startswith('j') and op not in ['jmp','jmpq']:branches.append(dict(address=m[1],instruction=ins))
   if '{%k' in ins and ins.startswith('vmov'):masked.append(ins)
   if op.startswith('cmov'):selects.append(ins)
  key=path.stem+('-prefetch' if 'read_loop<true' in name else '-direct')+('-full' if ', true,' in name else '-hits')
  if 'hint_range<' in name:
   key += '-range-'+('unpredictable' if re.search(r'hint_range<(?:true|false), (?:true|false), true,',name) else 'predictable')
  if 'hint_requests<' in name:key += '-caller'
  if a.precise_names:
   args=re.search(r'(read_loop|hint_range|hint_requests)<(true|false), (true|false)',name)
   assert args,name
   key=path.stem+('-prefetch' if args[2]=='true' else '-direct')+('-full' if args[3]=='true' else '-hits')
   if args[1]=='hint_range':
    method=re.search(r'hint_range<(?:true|false), (?:true|false), (true|false)',name)
    assert method,name
    key+='-range-'+('unpredictable' if method[1]=='true' else 'predictable')
   if args[1]=='hint_requests':key+='-caller'
   assert key not in {entry['loop'] for entry in proof},(key,name)
  text='\n'.join(body)
  if a.trim_separators:text=text.rstrip()
  (a.output/(key+'.asm')).write_text(text+'\n')
  proof.append(dict(binary=path.stem,function=name,loop=key,conditional_branches=branches,masked_value_loads=masked,conditional_moves=selects))
assert proof
(a.output/'branches.json').write_text(json.dumps(proof,indent=2)+'\n')
print(f'{len(proof)} complete native caller loops extracted; branch/masked-load/select inventory retained')
