#!/usr/bin/env python3
"""Compact manually inspected V2 callback paths from exact extraction receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import re

HERE=Path(__file__).resolve().parent


def compact_instruction(i):
    result={k:v for k,v in i.items() if k!='target'}
    if 'target' in i:
        target=i['target']
        result['target_symbol_sha256']=hashlib.sha256(target.encode()).hexdigest()
        result['target_category']='std_invoke_semantic_helper' if 'std::__invoke_impl<' in target else 'downstream_map' if 'get_downstream' in target else 'other'
        result['target_display']='std::__invoke_impl' if 'std::__invoke_impl<' in target else target[:180]
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extracted',type=Path,default=HERE/'shared-v2-codegen-extracted.json')
    parser.add_argument('--measurements',type=Path,default=HERE/'shared-v2-measurements.json')
    parser.add_argument('--x86-extracted',type=Path)
    parser.add_argument('--output',type=Path,default=HERE/'shared-v2-codegen.json')
    args=parser.parse_args()
    extracted=json.loads(args.extracted.read_text());measurements=json.loads(args.measurements.read_text())
    job=measurements['jobs'][0]
    result={'format':1,'status':'manually inspected V2 actual native paths, pending other architectures',
        'scope':'Hint1 metadata/payload valid continuing callback paths; rare finish/budget branches recorded, static instructions not cycles',
        'job_id':job['job_id'],'source_identity':measurements['expected_source_identity'],
        'ring_sha256':job['optimized_ring_sha256'],'assemblies':[],
        'findings':[
            'Native final continuation is an indirect BR, with countdown x21 carried in register, Ring x20 and Slot x22.',
            'Local Context/Group are reloaded together as16 bytes from Slot+16. Shared restores only Group8 bytes from Slot+16.',
            'Default hinted callbacks make ordinary std::__invoke_impl semantic helper calls: one at grain1, four at grain4. All active states execute those calls.',
            'Cold-positive hinted metadata inlines without ordinary helper calls or a frame; payload still outlines via std::__invoke_impl.',
            'Both default local/shared hinted stages use48-byte frames; local and shared have16-byte captured Map/Context closure atSP+8 and Context copy atSP+24.',
            'Shared has to reload Context x23 fromFP-8 before every continuing default handoff and payload handoff. Local reloads next Context from Slot instead.',
            'Shared reduces saved Slot argument bytes, but the actual default handoff is13 instructions vs local12 including stage target construction and frame restore.',
            'Cold-positive metadata continuing handoff is10 instructions in both signatures. Both use paired function/next load and one argument-load instruction.',
            'No clock, profile atomic or ordinary scheduler helper appears in the final continuing handoff; semantic helpers earlier in the same callback remain material.',
            'Slot64 local versus56 shared changes64-byte cache-line geometry. Static crossing model is recorded separately; no crossing penalty is measured.'
        ],'slot_geometry':measurements['source_audit']['slot_geometry_analysis'],
        'limits':['Generic reusable Cohort and earlier handwritten Cohort are distinct source cuts.',
                  'Cold/default timing factor changes outlining as well as the bytes>0 branch; its timing is not an isolated branch cost.',
                  'No throughput win is inferred from8 fewer saved argument bytes or static instruction counts.',
                  'This file currently qualifies V2 only; actual x86 files will be appended after receipt validation.']}
    for a in extracted['assemblies']:
        path=Path(a['path']);policy=path.name.removeprefix('assembly-neon-').removesuffix('.txt')
        common=policy.startswith('shared_');cold='_cold_' in policy;grain=int(policy[-1])
        item={'assembly':{k:a[k] for k in ['path','sha256','bytes']},'isa':'neon','policy':policy,
              'transfer':'shared' if common else 'local','window_mode':int(cold),'grain':grain,'callbacks':[]}
        for f in a['callbacks']:
            if f['hint_code']!=1:continue
            assert not f['atomic_or_clock_instructions']
            expected_calls=0 if cold and f['stage']=='metadata' else grain
            assert len(f['ordinary_calls'])==expected_calls
            assert all('std::__invoke_impl<' in i.get('target','') for i in f['ordinary_calls'])
            tail=next(t for t in f['tail_transfers'] if t['tail']['instruction']=='br x0')
            preceding=tail['preceding_instructions']
            start=max(k for k,i in enumerate(preceding) if i['instruction'].startswith('adrp x8,'))
            handoff=preceding[start:]+[tail['tail']]
            assert not any(re.match(r'^(bl|blr|callq?)\b',i['instruction']) for i in handoff)
            assert any(i['instruction']=='sub x21, x21, #0x1' for i in handoff)
            slot_load='ldr x24, [x22, #0x10]' if common else 'ldp x24, x23, [x22, #0x10]'
            assert any(i['instruction']==slot_load for i in handoff)
            frame=0 if cold and f['stage']=='metadata' else 48
            assert bool(f['stack_instructions'])==bool(frame)
            if frame:assert f['instructions'][0]['instruction']=='sub sp, sp, #0x30'
            reload=common and bool(frame)
            assert any(i['instruction']=='ldur x23, [x29, #-0x8]' for i in handoff)==reload
            expected_count=10 if not frame else 13 if common else 12
            assert len(handoff)==expected_count
            item['callbacks'].append({'stage':f['stage'],'hint_code':1,'address':f['address'],
                'ordinary_calls':[compact_instruction(i) for i in f['ordinary_calls']],
                'frame_bytes':frame,'prologue':[compact_instruction(i) for i in f['instructions'][:9]],
                'stack_instructions':[compact_instruction(i) for i in f['stack_instructions']],
                'continuing_handoff_instructions':len(handoff),'continuing_handoff':[compact_instruction(i) for i in handoff],
                'slot_argument_load_bytes':8 if common else 16,'shared_context_stack_reload_bytes':8 if reload else 0,
                'retire_tail':[compact_instruction(t['tail']) for t in f['tail_transfers'] if '::finish(' in t['tail'].get('target','')],
                'instruction_count_all_paths_and_padding':f['instruction_count_including_all_paths_and_padding']})
        assert len(item['callbacks'])==2
        result['assemblies'].append(item)
    assert len(result['assemblies'])==8
    if args.x86_extracted:
        x86=json.loads(args.x86_extracted.read_text())
        for a in x86['assemblies']:
            path=Path(a['path']);job_id=path.parts[-3]
            policy=path.name.removeprefix('assembly-avx512-').removesuffix('.txt')
            common=policy.startswith('shared_');cold='_cold_' in policy;grain=int(policy[-1])
            parent=next(j for j in measurements['jobs'] if j['job_id']==job_id)
            assert parent['optimized_ring_sha256']==result['ring_sha256']
            collection=json.loads((path.parent.parent/'collection.json').read_text())
            assert collection['members'][path.name]['sha256']==a['sha256']
            item={'assembly':{k:a[k] for k in ['path','sha256','bytes']},'isa':'avx512','job_id':job_id,
                  'machine':parent['machine'],'policy':policy,'transfer':'shared' if common else 'local',
                  'window_mode':int(cold),'grain':grain,'callbacks':[]}
            for f in a['callbacks']:
                if f['hint_code']!=1:continue
                assert not f['atomic_or_clock_instructions']
                expected_calls=0 if cold and f['stage']=='metadata' else grain
                assert len(f['ordinary_calls'])==expected_calls
                assert all('std::__invoke_impl<' in i.get('target','') for i in f['ordinary_calls'])
                ins=f['instructions'];tail_index=next(k for k,i in enumerate(ins) if re.match(r'jmpq? \*%rax$',i['instruction']))
                start=max(k for k,i in enumerate(ins[:tail_index]) if re.match(r'leaq .*\(%rip\), %rax',i['instruction']))
                handoff=ins[start:tail_index+1]
                assert not any(re.match(r'^callq?\b',i['instruction']) for i in handoff)
                assert any(i['instruction']=='decq %r13' for i in handoff)
                assert any(i['instruction']=='movq 0x10(%r14), %rdi' for i in handoff)
                extra_local=any(i['instruction']=='movq 0x18(%r14), %r15' for i in handoff)
                assert extra_local==(not common)
                frame=0
                for i in ins[:3]:
                    if i['instruction'].startswith('pushq '):frame+=8
                    match=re.match(r'subq \$0x([0-9a-f]+), %rsp',i['instruction'])
                    if match:frame+=int(match[1],16)
                expected_frame=(0 if grain==1 else 8) if cold and f['stage']=='metadata' else 24 if f['stage']=='metadata' or grain==1 else 40
                assert frame==expected_frame
                reload=[i for i in handoff if re.match(r'movq (?:0x[0-9a-f]+)?\(%rsp\), %r15$',i['instruction'])]
                assert bool(reload)==(common and not(cold and f['stage']=='metadata'))
                expected_count=(11 if common else 12)+(grain==4) if cold and f['stage']=='metadata' else 13+(grain==4 and f['stage']=='payload')
                assert len(handoff)==expected_count
                item['callbacks'].append({'stage':f['stage'],'hint_code':1,'address':f['address'],
                    'ordinary_calls':[compact_instruction(i) for i in f['ordinary_calls']],
                    'frame_bytes':frame,'prologue':[compact_instruction(i) for i in ins[:10]],
                    'stack_or_rbp_instructions':[compact_instruction(i) for i in f['stack_instructions']],
                    'continuing_handoff_instructions':len(handoff),'continuing_handoff':[compact_instruction(i) for i in handoff],
                    'slot_argument_load_bytes':8 if common else 16,'shared_context_stack_reload_bytes':8 if reload else 0,
                    'retire_tail':[compact_instruction(t['tail']) for t in f['tail_transfers'] if '::finish(' in t['tail'].get('target','')],
                    'instruction_count_all_paths_and_padding':f['instruction_count_including_all_paths_and_padding']})
            assert len(item['callbacks'])==2
            result['assemblies'].append(item)
        assert len(result['assemblies'])==24
        result['status']='manually inspected V2 and Granite/Zen AVX512 native paths; measured negative std::invoke cut'
        result['findings'].extend([
            'Granite/Zen default hinted callbacks also make one/four ordinary std::__invoke_impl calls in both stages; cold metadata inlines, payload retains wrappers.',
            'X86 default grain1 stages have24-byte frames; grain4 metadata24-byte, payload40-byte. Cold metadata grain1 frame0 and grain4 RBP save8-byte.',
            'X86 Shared prefix still spills/reloads Context r15 from its closure slot; default common handoff instruction counts match local13 (payload grain4:14).',
            'Cold x86 metadata eliminates one per-Slot pointer-load instruction: local/shared12/11 at grain1,13/12 at grain4.',
            'Raw RBP instruction lists include RBP used for arithmetic in the kernel; frame byte conclusions use actual prologue pushes/stack subtraction.'
        ])
        result['limits']=[l for l in result['limits'] if 'currently qualifies V2 only' not in l]
        result['limits'].append('AVX2 measurements qualify independently, but the retained x86 assembly selection is AVX512 only.')
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(args.output,'verified',len(result['assemblies']),'assemblies',sum(len(a['callbacks']) for a in result['assemblies']),'callbacks; native paths retained separately by architecture')


if __name__=='__main__':main()
