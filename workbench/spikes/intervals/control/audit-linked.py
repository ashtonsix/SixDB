#!/usr/bin/env python3
"""Audit llvm-objdump complete chains and fused StreamPack callback pairs.

Normalizes only local branch addresses and PC-relative data displacement. Keeps
registers, constants, loads, spills and instruction order. An equal body does not
establish equal timing or identify a predictor/cache mechanism.
"""
import argparse,difflib,json,re
from pathlib import Path

def functions(path):
    result={};current=None
    for line in Path(path).read_text().splitlines():
        header=re.match(r'^([0-9a-f]+) <(.*)>:$',line)
        if header:
            current={'name':header[2],'start':int(header[1],16),'body':[],'calls':[]}
            result[header[2]]=current
        elif current and re.match(r'\s+[0-9a-f]+:',line):
            code=' '.join(line.split('\t')[1:]).strip()
            if re.search(r'\b(callq?|bl)\s',code):current['calls'].append(code)
            # Branch destinations retain their exact offset in this function.
            if re.match(r'(?:j\w+|b(?:\.[a-z]+)?|cbn?z|tbn?z)\s',code):
                code=re.sub(r'0x([0-9a-f]+) <.*>',lambda m:'LOCAL'+hex(int(m[1],16)-current['start']),code)
            # Loads still address the same absolute constant in this executable.
            if '%rip)' in code:code=re.sub(r'0x[0-9a-f]+\(%rip\)', 'DATA(%rip)',code)
            current['body'].append(code)
    return result

def compare(a,b,chain):
    left=a['body'];right=b['body']
    if chain:
        # These loops' first calls are cold abort/throw paths. Never hide a hot call.
        def hot(body):
            for i,code in enumerate(body):
                if re.search(r'\b(callq?|bl)\s',code):
                    if not any(x in code for x in ('abort@','__cxa_allocate_exception@')):
                        raise ValueError('unexpected first call: '+code)
                    return body[:i]
            return body
        left=hot(left);right=hot(right)
    else:
        while left and re.match(r'(?:nop\w*|int3)\b',left[-1]):left=left[:-1]
        while right and re.match(r'(?:nop\w*|int3)\b',right[-1]):right=right[:-1]
    return {'before':a['name'],'current':b['name'],'before_address':hex(a['start']),
            'current_address':hex(b['start']),'before_instructions':len(left),'current_instructions':len(right),
            'before_calls':a['calls'],'current_calls':b['calls'],
            'diff':list(difflib.unified_diff(left,right,n=2))}

p=argparse.ArgumentParser();p.add_argument('assembly');p.add_argument('output');args=p.parse_args()
fs=functions(args.assembly);rows=[]
for name,a in fs.items():
    if '::chain<' in name and 'intervals_before::sparse_sizes<' in name:
        target=name.replace('intervals_before::','intervals::')
        target=re.sub(r'sparse_sizes<([^>]+)>',r'sparse_sizes<\1, false>',target)
        if target in fs:rows.append(compare(a,fs[target],True))
    if 'streamv' in name and '_M_invoke' in name and 'EUlvE_E9' in name:
        target=name.replace('EUlvE_E9','EUlvE0_E9')
        if target in fs:rows.append(compare(a,fs[target],False))
if not rows:raise SystemExit('No matching complete caller pairs in this dump')
Path(args.output).write_text(json.dumps({'assembly':args.assembly,'pairs':rows},indent=2)+'\n')
print(f'{len(rows)} pairs; {sum(not r["diff"] for r in rows)} equal normalized bodies; inspect all residuals')
