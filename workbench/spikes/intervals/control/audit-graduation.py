#!/usr/bin/env python3
"""Compare exact linked StreamPack callback encodings across the ownership move.

Caller keys use geometry/carrier/lambda role, independent of the moved C++ enum
namespace. Includes full functions and trailing alignment; no operand stripping.
"""
import argparse,hashlib,json,re
from pathlib import Path

def callbacks(path):
    found={};active=None
    for line in Path(path).read_text().splitlines():
        h=re.match(r'^([0-9a-f]+) <(.*)>:$',line)
        if h:
            active=None;name=h[2]
            if 'streamv' not in name or '_M_invoke' not in name:continue
            layout=re.search(r'geometryE([012])E([hjt]+)St4byte([hjt])',name)
            role=re.search(r'EUlvE([01]?)_E9',name)
            if not layout or not role:raise ValueError('unrecognized callback '+name)
            key='/'.join([*layout.groups(),role[1] or 'maintained'])
            if key in found:raise ValueError('duplicate '+key)
            active={'symbol':name,'address':h[1],'encoding':[],'instructions':[]};found[key]=active
        elif active and re.match(r'\s+[0-9a-f]+:',line):
            parts=line.split('\t');encoding=parts[0].split(':',1)[1].strip()
            active['encoding'].append(''.join(encoding.split()))
            active['instructions'].append(' '.join(parts[1:]).strip())
    return found

p=argparse.ArgumentParser();p.add_argument('before');p.add_argument('after');p.add_argument('output');args=p.parse_args()
a=callbacks(args.before);b=callbacks(args.after)
assert a and a.keys()==b.keys(),'callback set differs'
rows=[]
for key,left in a.items():
    right=b[key]
    rows.append({'key':key,'before_address':left['address'],'after_address':right['address'],
      'before_instructions':len(left['encoding']),'after_instructions':len(right['encoding']),
      'before_encoding_sha256':hashlib.sha256(''.join(left['encoding']).encode()).hexdigest(),
      'after_encoding_sha256':hashlib.sha256(''.join(right['encoding']).encode()).hexdigest(),
      'exact_encoding_equal':left['encoding']==right['encoding'],
      'same_linked_address':left['address']==right['address']})
Path(args.output).write_text(json.dumps({'scope':'complete linked StreamPack callbacks including alignment; exact encoded instruction text','before':args.before,'after':args.after,'pairs':rows},indent=2)+'\n')
print(len(rows),'callbacks;',sum(r['exact_encoding_equal'] for r in rows),'exact instruction encodings;',sum(r['same_linked_address'] for r in rows),'same linked address')
