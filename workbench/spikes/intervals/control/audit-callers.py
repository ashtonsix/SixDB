#!/usr/bin/env python3
"""Compare linked actual Directory read functions, preserving operands/symbols.

Only historical namespace spelling, instruction addresses before named symbols,
comments and whitespace are normalized. Moved x86 RIP-relative references retain
the resolved absolute target. Other immediates and symbolic offsets remain.
This is a codegen diagnostic, not a semantic proof or a runtime measurement.
"""
import argparse,hashlib,json,re
from pathlib import Path

def functions(path, stream=False):
 result={};active=None
 for line in Path(path).read_text().splitlines():
  h=re.match(r'^([0-9a-f]+) <(.*)>:$',line)
  if h:
   active=None
   name=h[2].replace('ikea::boundlist::','ikea::intervals::')
   if stream:
    if 'streamv' not in name or '_M_invoke' not in name:continue
    layout=re.search(r'geometryE([012])E([hjt]+)St4byte([hjt])',name)
    role=re.search(r'EUlvE([01]?)_E9',name)
    if not layout or not role:raise ValueError('unrecognized callback '+name)
    name='/'.join([*layout.groups(),role[1] or 'maintained'])
   elif not re.match(r'engine::directory::Layer::(route|lower|find|seek|dirent|next)\(',name):continue
   active={'address':h[1],'instructions':[],'encoding':[]};result[name]=active
  elif active and re.match(r'\s+[0-9a-f]+:',line):
   parts=line.split('\t');encoding=parts[0].split(':',1)[1].strip()
   instruction=' '.join(parts[1:]).strip().replace('ikea::boundlist::','ikea::intervals::')
   # A moved x86 instruction has a different RIP displacement to the same
   # absolute constant. Preserve that target, rather than erasing the operand.
   target=re.search(r'# (0x[0-9a-f]+) <',instruction)
   if target:instruction=re.sub(r'0x[0-9a-f]+\(%rip\)', '@'+target[1],instruction)
   instruction=re.sub(r'0x[0-9a-f]+ (?=<)','',instruction)
   instruction=instruction.split('//')[0].strip()
   active['encoding'].append(''.join(encoding.split()));active['instructions'].append(' '.join(instruction.split()))
 return result

def digest(rows):return hashlib.sha256('\n'.join(rows).encode()).hexdigest()
p=argparse.ArgumentParser(description=__doc__);p.add_argument('before');p.add_argument('after');p.add_argument('output');p.add_argument('--stream',action='store_true');a=p.parse_args()
left,right=functions(a.before,a.stream),functions(a.after,a.stream)
assert left and left.keys()==right.keys(),'caller function set differs'
rows=[]
for name,l in left.items():
 r=right[name]
 rows.append({'function':name,'before_address':l['address'],'after_address':r['address'],
              'before_instructions':len(l['instructions']),'after_instructions':len(r['instructions']),
              'exact_encoding_equal':l['encoding']==r['encoding'],
              'normalized_instructions_equal':l['instructions']==r['instructions'],
              'before_normalized_sha256':digest(l['instructions']),'after_normalized_sha256':digest(r['instructions'])})
Path(a.output).write_text(json.dumps({'scope':__doc__,'pairs':rows},indent=2)+'\n')
print(len(rows),'actual caller functions;',sum(r['exact_encoding_equal'] for r in rows),'exact encodings;',sum(r['normalized_instructions_equal'] for r in rows),'normalized instruction/operand/symbol matches')
