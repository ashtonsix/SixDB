#!/usr/bin/env python3
"""Inventory complete Worker mutation dispatch; static counts are not hot costs."""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path


def inspect(path):
    opener=gzip.open if path.suffix=='.gz' else open
    bodies={}; current=None
    with opener(path,'rt') as f:
        for line in f:
            match=re.match(r'^([0-9a-f]+) <(.+)>:$',line.strip())
            if match:
                current=match[2]; bodies[current]=[]
            elif current and re.match(r'^\s*[0-9a-f]+:\s+\S+',line):bodies[current].append(line.strip())
    roots=[name for name in bodies if 'ht::epochs::candidate::visit<' in name and 'candidate::apply(' in name]
    assert len(roots)==1,(path,roots)
    result={}
    for name in roots:
        body=bodies[name]
        calls=[line for line in body if re.search(r'\s(?:callq?|bl|blr)\s',line)]
        result[name]={'instructions':len(body),'calls':calls,'entry':body[:24],
            'explicit_vector_multiply_instructions':[line for line in body if re.search(r'\b(?:u?mull2?|umlal2?|vpmuludq|pmuludq)\b',line)],
            'stack_references':[line for line in body if re.search(r'\[sp(?:,|\])|\(%[re]sp\)',line)]}
    return {'assembly_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'roots':result}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    files=sorted(a.root.glob('*.s.gz'));assert files
    out={'scope':'One epoch dispatcher per eight records; includes growth/refusal/cold calls. Instruction, call and stack-reference counts are static, not a dynamic profile or spill count.',
         'files':{f.name:inspect(f) for f in files}}
    a.output.write_text(json.dumps(out,indent=2)+'\n')
    print(f'{len(files)} complete mutation dispatchers inventoried')
