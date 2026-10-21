#!/usr/bin/env python3
"""Check barrier emission and unchanged primary-loop instructions/registers."""
import argparse
import json
from pathlib import Path
import re

p=argparse.ArgumentParser()
p.add_argument('root',type=Path,help='Worker results directory with assembly/')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)

def sections(path):
    lines=path.read_text().splitlines();result={};start=None;name=None
    for i,line in enumerate(lines):
        if re.match(r'^[0-9a-f]+ <',line):
            if start is not None:result[name]=lines[start:i]
            name=line.split(' <',1)[1].rsplit('>:',1)[0];start=i
    if start is not None:result[name]=lines[start:]
    return result

def normalize(lines):
    result=[]
    for line in lines[1:]:
        match=re.match(r'\s*[0-9a-f]+:\s+(.*)',line)
        if not match:continue
        text=match[1].split('#',1)[0].strip()
        if text.startswith(('nop','data16','cs ')):continue
        text=re.sub(r'0x[0-9a-f]+(?= <)','TARGET',text)
        text=re.sub(r'[-+]?0x[0-9a-f]+\(%rip\)','RIP(%rip)',text)
        result.append(re.sub(r'\s+',' ',text))
    return result

proof=[]
for w,v,b in [(32,0,11),(32,8,11),(40,0,12),(40,8,11)]:
    stem=f'memory_ikea_{w}_{b}_{v}'
    base=sections(a.root/'assembly'/f'base-{stem}.asm')
    for variant in ['lfence']+(['cpuid'] if (w,v)==(40,0) else []):
        candidate=sections(a.root/'assembly'/f'{variant}-{stem}.asm')
        helpers=[(k,body) for k,body in candidate.items() if '::read_overflow(' in k]
        assert len(helpers)==1
        assert any(re.search(r'\b'+variant+r'\b',line) for line in helpers[0][1])
        (a.output/f'{variant}-{stem}-helper.asm').write_text('\n'.join(helpers[0][1])+'\n')
        for name,body in base.items():
            if 'read_loop<false' not in name:continue
            before,after=normalize(body),normalize(candidate[name])
            assert before==after,(stem,variant,name)
            proof.append(dict(w=w,v=v,variant=variant,
                sink='full' if 'false, true' in name else 'hits',
                normalized_primary_loop_identical=True,
                base_instructions=len(before),variant_instructions=len(after)))
assert len(proof)==10
(a.output/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print('Ten primary-loop pairs match after relocation/padding normalization; barriers present.')
