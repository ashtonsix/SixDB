#!/usr/bin/env python3
"""Seal actual native header-factor call/frame/spill/tail evidence."""
import argparse
import json
from pathlib import Path
import re

from summarize_shared_codegen import compact_instruction

HERE=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extracted',type=Path,default=HERE/'cohort-factor-codegen-extracted.json')
    parser.add_argument('--measurements',type=Path,default=HERE/'cohort-factor-measurements.json')
    parser.add_argument('--output',type=Path,default=HERE/'cohort-factor-codegen.json')
    args=parser.parse_args();extracted=json.loads(args.extracted.read_text());measure=json.loads(args.measurements.read_text())
    jobs={j['job_id']:j for j in measure['jobs']}
    result={'format':1,'qualification':'actual old/new assembly, calls/frames/tails independently inspected',
        'source_identity':measure['source_identity'],'ring_sha256':measure['source_audit']['ring_sha256'],
        'cohort_header_sha256':measure['source_audit']['header_sha256'],'assemblies':[],
        'findings':[
            'All direct native callback specializations have zero ordinary std::__invoke_impl calls on V2, Granite AVX512 and Zen AVX512.',
            'Metadata has no ordinary calls in either cold-positive arm. Old payload has one/four invoke sites, direct payload only conditional downstream Map fallback sites (Hint1: one static site).',
            'The final continuing transfer is BR on ARM and indirect JMP on x86, with one8-byte Slot argument load and no ordinary helper/clock/profile atomic in the tail segment.',
            'Direct grain4 payload changes to a bounded loop with register pressure: countdown and Ring/Slot/Group state spill even though the callback-to-callback transfer still carries them in registers.',
            'V2 payload frames old/direct: grain1 48/16 bytes, grain4 48/48 bytes. Metadata has no frame.',
            'Both x86 hosts payload frames old/direct: grain1 24/40 bytes, grain4 40/72 bytes. Metadata frames grain1 0, grain4 RBP8 bytes.',
            'V2 continuing payload segment old/direct: grain1 13/11 instructions, grain4 13/15; x86 grain1 13/13, grain4 14/17. Includes necessary state restores after the empty-group branch.',
            'Header factor includes direct invocation and stable states-pointer snapshot together. Static instruction counts are not cycles or isolated costs.'
        ],'limits':['Actual framework helper removal is proven; a throughput win at every occupancy is not.',
                  'Downstream fallback remains synchronous. Static site counts do not count executed fallback calls.',
                  'No PMU/cache-tier inference from these uninstrumented timings. All source/ABI and hardware contexts are retained separately.']}
    for a in extracted['assemblies']:
        path=Path(a['path']);job_id=path.parts[-3];j=jobs[job_id]
        m=re.fullmatch(r'assembly-(neon|avx512)-cohort_(invoke|direct)_grain([14])\.txt',path.name);assert m
        isa,call,g=m.groups();grain=int(g)
        collection=json.loads((path.parent.parent/'collection.json').read_text());assert collection['members'][path.name]['sha256']==a['sha256']
        reported=j['reported_codegen'][isa][f'cohort_{call}_grain{grain}']
        assert reported['assembly_sha256']==a['sha256']
        callbacks=a['callbacks'];assert len(callbacks)==8
        invoke=sum('std::__invoke_impl<' in i.get('target','') for f in callbacks for i in f['ordinary_calls'])
        assert invoke==reported['invoke_impl_callback_calls']==(0 if call=='direct' else 4*grain)
        item={'job_id':job_id,'machine':j['machine'],'isa':isa,'call_factor':call,'grain':grain,
              'assembly':{k:a[k] for k in ['path','sha256','bytes']},'all_specializations_invoke_sites':invoke,'callbacks':[]}
        for f in callbacks:
            assert not f['atomic_or_clock_instructions']
            calls=f['ordinary_calls']
            if f['stage']=='metadata':assert not calls
            elif call=='invoke':assert len(calls)==grain and all('std::__invoke_impl<' in i.get('target','') for i in calls)
            else:assert calls and all('get_downstream' in i.get('target','') for i in calls)
            if f['hint_code']!=1:continue
            ins=f['instructions']
            tail_index=next(k for k,i in enumerate(ins) if i['instruction'] in ['br x0','jmpq *%rax'])
            prefix=ins[:tail_index]
            if isa=='neon':
                start=max(k for k,i in enumerate(prefix) if i['instruction'].startswith('adrp x8,'))
                if f['stage']=='payload':
                    before=max(k for k,i in enumerate(prefix[:start]) if i['instruction'].startswith('cbz '))
                    start=before+1
                frame=0
                for i in ins[:3]:
                    sub=re.match(r'sub sp, sp, #0x([0-9a-f]+)',i['instruction'])
                    pre=re.search(r'\[sp, #-0x([0-9a-f]+)\]!',i['instruction'])
                    if sub:frame+=int(sub[1],16)
                    if pre:frame+=int(pre[1],16)
                expected_frame=0 if f['stage']=='metadata' else 16 if call=='direct' and grain==1 else 48
                expected_count=10 if f['stage']=='metadata' else 13 if call=='invoke' else 11 if grain==1 else 15
                stack=[i for i in ins if re.search(r'\[(?:sp|x29)\b',i['instruction']) or re.search(r'\bsp\b',i['instruction'])]
                assert any(i['instruction']=='ldr x24, [x22, #0x10]' for i in ins[start:tail_index+1])
            else:
                start=max(k for k,i in enumerate(prefix) if re.match(r'leaq .*\(%rip\), %rax',i['instruction']))
                if f['stage']=='payload':
                    before=max(k for k,i in enumerate(prefix[:start]) if i['instruction'].startswith('je '))
                    start=before+1
                frame=0
                for i in ins[:3]:
                    if i['instruction'].startswith('pushq '):frame+=8
                    sub=re.match(r'subq \$0x([0-9a-f]+), %rsp',i['instruction'])
                    if sub:frame+=int(sub[1],16)
                expected_frame=(0 if grain==1 else 8) if f['stage']=='metadata' else (24 if grain==1 else 40) if call=='invoke' else (40 if grain==1 else 72)
                expected_count=11+(grain==4) if f['stage']=='metadata' else 13+(grain==4) if call=='invoke' else 13 if grain==1 else 17
                stack=[i for i in ins if '%rsp' in i['instruction'] or re.match(r'^(pushq|popq)\b',i['instruction']) or '(%rbp)' in i['instruction']]
                assert any(i['instruction']=='movq 0x10(%r14), %rdi' for i in ins[start:tail_index+1])
            handoff=ins[start:tail_index+1]
            assert frame==expected_frame and len(handoff)==expected_count,(path.name,f['stage'],frame,len(handoff))
            assert not any(re.match(r'^(bl|blr|callq?)\b',i['instruction']) for i in handoff)
            item['callbacks'].append({'stage':f['stage'],'hint_code':1,'address':f['address'],'frame_bytes':frame,
                'ordinary_call_sites':[compact_instruction(i) for i in calls],'prologue':[compact_instruction(i) for i in ins[:16]],
                'stack_operations':[compact_instruction(i) for i in stack],
                'continuing_handoff_instructions':len(handoff),'continuing_handoff':[compact_instruction(i) for i in handoff],
                'saved_slot_argument_bytes':8,'budget_spilled_in_body':call=='direct' and grain==4 and f['stage']=='payload',
                'common_tail_clock_profile_atomic':False,'common_tail_ordinary_helper':False})
        assert len(item['callbacks'])==2;result['assemblies'].append(item)
    assert len(result['assemblies'])==12
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(args.output,'12 native assemblies/96 specializations independently checked,24 actual Hint1 paths retained')


if __name__=='__main__':main()
