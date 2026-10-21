#!/usr/bin/env python3
"""Retain complete native read loops and their conditional branches for review.

Counts describe static code, including rare helpers; they do not assert an
unexecuted branch is costly or prove a data dependency from a mnemonic count.
"""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
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
  if 'read_loop<' not in name:continue
  branches=[];masked=[];selects=[]
  for line in body[1:]:
   m=re.match(r'\s*([0-9a-f]+):\s+(.*)',line)
   if not m:continue
   ins=m[2];op=ins.split()[0]
   if op.startswith('j') and op not in ['jmp','jmpq']:branches.append(dict(address=m[1],instruction=ins))
   if '{%k' in ins and ins.startswith('vmov'):masked.append(ins)
   if op.startswith('cmov'):selects.append(ins)
  key=path.stem+('-prefetch' if 'read_loop<true' in name else '-direct')+('-full' if ', true,' in name else '-hits')
  (a.output/(key+'.asm')).write_text('\n'.join(body)+'\n')
  proof.append(dict(binary=path.stem,function=name,loop=key,conditional_branches=branches,masked_value_loads=masked,conditional_moves=selects))
assert proof
(a.output/'branches.json').write_text(json.dumps(proof,indent=2)+'\n')
print(f'{len(proof)} complete native caller loops extracted; branch/masked-load/select inventory retained')
