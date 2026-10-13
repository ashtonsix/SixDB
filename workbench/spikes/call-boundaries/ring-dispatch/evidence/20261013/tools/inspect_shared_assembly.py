#!/usr/bin/env python3
"""Extract actual callback instructions for manual native handoff/spill audit.

Counts are static function/path evidence, never modeled cycles or PMU results.
Tab fields distinguish machine bytes from mnemonics such as add/adc/dec.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

HERE=Path(__file__).resolve().parent
HEADER=re.compile(r'^([0-9a-f]+) <(.+)>:$')
CALLBACK=re.compile(r'^void (metadata|payload)<(true|false),.*\(loom::Hint\)([0-3])>\(')


def extract(path):
    functions=[];current=None
    for number,line in enumerate(path.read_text().splitlines(),1):
        match=HEADER.match(line)
        if match:
            current={'address':'0x'+match[1],'symbol':match[2],'line':number,'instructions':[]}
            functions.append(current);continue
        if current is None or '\t' not in line:continue
        fields=line.split('\t')
        prefix=re.match(r'\s*([0-9a-f]+):\s*([0-9a-f ]+)\s*$',fields[0])
        if not prefix:continue
        full=' '.join(x.strip() for x in fields[1:] if x.strip())
        text,_,target=full.partition(' <')
        instruction={'address':'0x'+prefix[1],'instruction':text,'line':number}
        if target:instruction['target']=target.rstrip('>')
        current['instructions'].append(instruction)
    callbacks=[]
    for f in functions:
        match=CALLBACK.match(f['symbol'])
        if not match:continue
        f.update(stage=match[1],hints=match[2]=='true',hint_code=int(match[3]))
        ins=f['instructions']
        f['ordinary_calls']=[i for i in ins if re.match(r'^(callq?|bl|blr)\b',i['instruction'])]
        f['stack_instructions']=[i for i in ins if re.search(r'(\bsp\b|%rsp|%rbp)',i['instruction']) or re.match(r'^(pushq?|popq?)\b',i['instruction'])]
        f['tail_transfers']=[{'tail':i,'preceding_instructions':ins[max(0,k-14):k]} for k,i in enumerate(ins)
                             if re.match(r'^(br\s|jmpq?\s+\*)',i['instruction']) or
                             (re.match(r'^(b|jmpq?)\s',i['instruction']) and ('::finish(' in i.get('target','') or '::resume(' in i.get('target','')))]
        f['atomic_or_clock_instructions']=[i for i in ins if re.search(r'(\b(ldar|stlr|ldaxr|stlxr|lock|rdtsc|rdtscp)\b|clock_gettime|HardwareProfile)',i['instruction']+' '+i.get('target',''))]
        f['instruction_count_including_all_paths_and_padding']=len(ins)
        callbacks.append(f)
    assert callbacks,('no callbacks',path)
    return {'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size,
            'scope':'exact native callback instruction evidence; static counts include cold blocks and padding',
            'callbacks':callbacks,
            'scheduler_resume_functions':[f for f in functions if '::resume(' in f['symbol'] and 'MemoryRing<' in f['symbol']]}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assemblies',type=Path,nargs='+')
    parser.add_argument('--output',type=Path,default=HERE/'shared-codegen-extracted.json')
    args=parser.parse_args()
    result={'format':1,'status':'extracted for manual audit, not a handoff conclusion', 'assemblies':[extract(p) for p in args.assemblies]}
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(args.output,[(a['path'],len(a['callbacks'])) for a in result['assemblies']])


if __name__=='__main__':main()
