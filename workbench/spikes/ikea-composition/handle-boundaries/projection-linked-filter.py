#!/usr/bin/env python3
"""Offline linked projection filter audit; preserves the historical R4 helper.

Trace admitted full/tight/in-domain nonterminal dependent and independent8
iterations in actual optimized benchmark functions. Unknown branches fail.
No compiler, timing, source mutation or inferred microarchitecture causality.
"""
import argparse
from collections import Counter
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex

ROOT = Path(__file__).resolve().parents[4]
HELPER = ROOT / 'workbench/spikes/ikea-composition/handle-boundaries/linked-filter.py'
spec = importlib.util.spec_from_file_location('historical_linked_filter', HELPER)
old = importlib.util.module_from_spec(spec)
spec.loader.exec_module(old)
historical_role = old.role
def role(name):
    if '::completion<' in name and 'extras::finish<1u>' in name:
        return 'common/completion1'
    return historical_role(name)
old.role = role


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashed(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def target(row):
    match = re.search(r'0x([0-9a-f]+) <', row)
    assert match, row
    return int(match[1], 16)


def choose(row, flags, start):
    mnemonic = row.split('\t', 1)[0]
    # The typed predicate has exactly nonempty activity, in-domain RHS, full
    # activity and unit stride guards. The loop itself is nonterminal.
    if flags.startswith('test'):
        assert mnemonic in ('je', 'jne'), (row, flags)
        return mnemonic == 'jne', 'nonempty activity'
    if re.match(r'^(?:cmpw\t\$(?:-0x1|-1),|cmpl\t\$0xffff,)', flags):
        assert mnemonic in ('je', 'jne'), (row, flags)
        return mnemonic == 'je', 'all16 rows active'
    if re.match(r'^cmpq\t\$0x1,', flags):
        assert mnemonic in ('je', 'jne'), (row, flags)
        return mnemonic == 'je', 'unit stride'
    if flags.startswith('cmpq\t%'):
        assert mnemonic in ('ja', 'jbe'), (row, flags)
        return mnemonic == 'jbe', 'RHS within field domain'
    if flags.startswith(('cmpl', 'cmpq')) and start is not None:
        assert mnemonic in ('je', 'jne'), (row, flags)
        assert target(row) != start or mnemonic == 'jne', (row, flags, start)
        return target(row) == start, 'nonterminal iteration'
    raise ValueError(('unclassified branch', row, flags))


def walk(function, start=None):
    rows = {a: text for a, _, text in function['rows']}
    following = dict(zip(rows, list(rows)[1:]))
    address = function['address'] if start is None else start
    walked, conditions, flags = [], [], ''
    while not walked or address != start:
        assert len(walked) < 1200, (function['name'], hex(address))
        row = rows[address]
        walked.append([hex(address), row])
        op = row.split('\t', 1)[0]
        if op.startswith(('cmp', 'test')):
            flags = row
        if op == 'retq' or (op in ('jmp', 'jmpq') and row.split('\t', 1)[1].startswith('*')):
            assert start is None, (function['name'], 'unexpected loop exit', row)
            break
        if op in ('jmp', 'jmpq'):
            address = target(row)
        elif op.startswith('j'):
            taken, premise = choose(row, flags, start)
            conditions.append({'address': hex(address), 'instruction': row,
                               'last_test': flags, 'taken': taken, 'premise': premise})
            address = target(row) if taken else following[address]
        else:
            address = following[address]
    opcodes = Counter(row.split('\t', 1)[0] for _, row in walked)
    compares = sum(n for op, n in opcodes.items() if op.startswith('vpcmp'))
    assert compares == (1 if start is None or len(conditions) < 12 else 8), (function['name'], compares, conditions)
    assert not any(op.startswith('call') for op in opcodes), function['name']
    if start is None:
        assert len(conditions) == 4, (function['name'], conditions)
    return {'instructions': len(walked), 'opcodes': dict(opcodes), 'conditions': conditions, 'trace': walked,
            'trace_sha256': hashed(walked), 'entry': hex(start or function['address'])}


def inline(function, mode):
    rows = {a: text for a, _, text in function['rows']}
    following = dict(zip(rows, list(rows)[1:]))
    timers = [a for a, row in rows.items() if 'callq' in row and '_ZNSt6chrono3_V212steady_clock3nowEv' in row]
    assert len(timers) == 2
    # The second emitted timer site is the start timer; its following iteration
    # count and serial flag branches admit the two timed routes.
    prefix = [(a, row) for a, row in rows.items() if a > timers[1]][:18]
    branches = [(a, row) for a, row in prefix if row.startswith('je\t')]
    assert len(branches) == 2 and 'cmpb\t$0x0,' in rows[list(rows)[list(rows).index(branches[1][0])-1]], (function['name'], prefix)
    initial = target(branches[1][1]) if mode == 'independent8' else following[branches[1][0]]
    setup = [(a, row) for a, row in rows.items() if a >= initial][:18]
    jumps = [(a, row) for a, row in setup if row.startswith('jmp\t')]
    assert jumps, (function['name'], mode, setup)
    path = walk(function, target(jumps[0][1]))
    assert sum(n for op, n in path['opcodes'].items() if op.startswith('vpcmp')) == (8 if mode == 'independent8' else 1)
    assert len(path['conditions']) == (33 if mode == 'independent8' else 5)
    path['scope'] = 'One nonterminal ' + mode + ' iteration, full activity, unit stride, in-domain RHS.'
    return path


def skeleton(row):
    body = row.split('#', 1)[0].strip()
    body = re.sub(r'-?0x[0-9a-f]+\(%rip\)', 'ADDRESS(%rip)', body)
    if re.match(r'^(?:j\w*|callq)\t0x', body):
        body = re.sub(r'0x[0-9a-f]+ <', 'TARGET <', body, count=1)
    return body


def payload(elf, address, instruction):
    # The historical helper's full-vector fallback is suitable for vector
    # memory operands; a broadcast reads a scalar, irrespective of output width.
    mnemonic = instruction.split('\t', 1)[0]
    lengths = {'vpbroadcastb':1, 'vpbroadcastw':2, 'vpbroadcastd':4,
               'vpbroadcastq':8, 'vbroadcastss':4, 'vbroadcastsd':8}
    if mnemonic not in lengths:
        return elf.payload(address, instruction)
    length = lengths[mnemonic]
    data = elf.read(address, length)
    return {'address':hex(address), 'section':elf.section(address)[0], 'size':length,
            'kind':'exact scalar broadcast operand', 'bytes':data.hex(),
            'sha256':hashlib.sha256(data).hexdigest(), 'symbols':[]}


def compare_layout(before, after, elfs):
    result = {'same_relative_offsets': False, 'instruction_skeletons_equal': False, 'references': []}
    if len(before['rows']) != len(after['rows']):
        return result
    result['same_relative_offsets'] = all(b[0]-before['address'] == a[0]-after['address']
                                        for b, a in zip(before['rows'], after['rows']))
    result['instruction_skeletons_equal'] = all(skeleton(b[2]) == skeleton(a[2])
                                              for b, a in zip(before['rows'], after['rows']))
    if not result['instruction_skeletons_equal']:
        result['differences'] = [[hex(b[0]), b[2], hex(a[0]), a[2]]
            for b, a in zip(before['rows'], after['rows']) if skeleton(b[2]) != skeleton(a[2])]
        return result
    for b, a in zip(before['rows'], after['rows']):
        if '(%rip)' in b[2]:
            assert '(%rip)' in a[2]
            operands = {arm: payload(elfs[arm],int(re.search(r'# 0x([0-9a-f]+)', row[2])[1],16), row[2])
                       for arm, row in (('before',b),('after',a))}
            result['references'].append({'offset': b[0]-before['address'], **operands,
                'payload_equal': operands['before']['bytes'] == operands['after']['bytes']})
    result['all_addressed_payloads_equal'] = all(p['payload_equal'] for p in result['references'])
    return result


def compact(result, summary, output):
    retained = {k:v for k,v in result.items() if k != 'profiles'}
    retained['full_receipt'] = {'path':str(output/'receipt.json'), 'sha256':sha(output/'receipt.json')}
    retained['profiles'] = {}
    for profile,pair in result['profiles'].items():
        item = {'inputs':{}, 'functions':{'columns':['route','before_address','after_address','before_size','after_size',
                    'before_instructions','after_instructions','before_sha256','after_sha256','raw_bytes_equal'],'rows':[]},
                'paths':{'columns':['route','kind','before_entry','after_entry','before_instructions','after_instructions',
                    'before_instruction_bytes','after_instruction_bytes','before_conditions','after_conditions',
                    'before_trace_sha256','after_trace_sha256','opcode_differences'],'rows':[]},
                'selected_path_details':{},'shared_route_proofs':{},'payloads':{},'hoisted_sign_broadcasts':{}}
        for arm,entry in pair['inputs'].items():
            item['inputs'][arm]={k:v for k,v in entry.items() if k!='configured_filter_commands'}
            item['inputs'][arm]['configured_filter_command_count']=len(entry['configured_filter_commands'])
            item['inputs'][arm]['configured_filter_commands_digest']=hashed(entry['configured_filter_commands'])
        for key,f in pair['functions'].items():
            before,after=f['before'],f['after']
            item['functions']['rows'].append([key,before['address'],after['address'],before['size'],after['size'],
                before['instructions'],after['instructions'],before['bytes_sha256'],after['bytes_sha256'],f['raw_bytes_equal']])
            if 'layout_comparison' in f:
                comparison=f['layout_comparison']
                proof={k:v for k,v in comparison.items() if k!='references'}
                proof['references']={'columns':['function_offset','before_address','after_address','before_payload_sha256','after_payload_sha256'],
                                     'rows':[]}
                for ref in comparison['references']:
                    proof['references']['rows'].append([ref['offset'],ref['before']['address'],ref['after']['address'],
                                                       ref['before']['sha256'],ref['after']['sha256']])
                    for arm in ('before','after'):
                        payload=ref[arm]
                        item['payloads'][payload['sha256']]={k:payload[k] for k in ('bytes','size','kind')}
                item['shared_route_proofs'][key]=proof
            for kind in f['before']['paths']:
                paths={arm:f[arm]['paths'][kind] for arm in ('before','after')}
                detail={}
                detailed = ((profile=='avx2' and key in ('w5/op0/inline_measure','w8/op0/inline_measure',
                             'w5/op2/inline_measure','w8/op2/inline_measure') and kind=='dependent') or
                            (profile=='avx512' and key in ('w5/op1/one_stage','w5/op2/one_stage',
                             'w8/op1/one_stage','w8/op2/one_stage')))
                for arm,p in paths.items():
                    if detailed:
                        detail[arm]={'payloads':p['payloads'],'occupied_64byte_code_regions':p['occupied_64byte_code_regions'],
                            'boundary_crossings':[{k:c[k] for k in ('address','bytes','offset_mod64')}
                                                  for c in p['boundary_crossing_instructions']]}
                differences={op:[paths['before']['opcodes'].get(op,0),paths['after']['opcodes'].get(op,0)]
                    for op in sorted(set(paths['before']['opcodes'])|set(paths['after']['opcodes']))
                    if paths['before']['opcodes'].get(op,0)!=paths['after']['opcodes'].get(op,0)}
                b,a=paths['before'],paths['after']
                item['paths']['rows'].append([key,kind,b['entry'],a['entry'],b['instructions'],a['instructions'],
                    b['instruction_bytes'],a['instruction_bytes'],len(b['conditions']),len(a['conditions']),
                    b['trace_sha256'],a['trace_sha256'],differences])
                if detail:
                    item['selected_path_details'][key+'/'+kind]=detail
            if profile=='avx2' and key in ('w5/op2/inline_measure','w8/op2/inline_measure'):
                item['hoisted_sign_broadcasts'][key] = f['after']['broadcast_memory_operands']
        retained['profiles'][profile]=item
    native=list(csv.DictReader((summary/'paired-cases.csv').open()))
    def timing(row):
        return {k:(json.loads(v) if k.endswith('_samples') else float(v) if k not in
                   ('member','profile','family','case','metric','trials') else int(v) if k=='trials' else v)
                for k,v in row.items()}
    selected=[timing(row) for row in native if row['profile'] in ('avx2','avx512') and
              row['family']=='filters' and float(row['after_over_before_median'])>=1.05]
    assert len(selected)==19
    for row in selected:
        parts=row['case'].split('/')
        assert parts[0]=='tuple_tight' and parts[3]=='65535'
        row['classification']='shared native decode/compare route; prepared predicate bypassed' if parts[4]=='shared' else 'prepared predicate full-tight path'
    retained['measured_losses_ge_5_percent']=selected
    retained['boundary_pattern_counterexample']=timing(next(row for row in native if row['profile']=='avx512'
        and row['case']=='tuple_tight/8/2/65535/one_stage/independent8'))
    retained['observations']=[
        'All39selected complete functions/profile are verified against symbol-sized ELF extents; all19 x86 >=5% native paired-median losses are retained, including4AVX2 inline/dependent,3AVX512 one_stage/independent8 and12shared controls.',
        'Common entry and completion are raw-byte exact at unchanged addresses. The indirect timer has equal instruction skeletons/offsets and13 RIP data references to equal initial payloads per profile.',
        'AVX512 native decode and all5 joined comparisons are raw-byte exact at moved addresses. AVX2 counterparts retain exact instruction skeletons/offsets, with6decode and4joined RIP constant references verified equal.',
        'The shared route bypasses the prepared predicate. Its measured losses occur through unchanged instructions/data with different linked layout/context; that is not evidence of newly added predicate work or a reason to dismiss the losses as noise.',
        'Full-tight execution already used copied code before the change: no full plan access is removed from that path. Its generic-fallback, lifetime and residence benefit must be interpreted separately.',
        'AVX2 dependent equal43to39 and less_equal45to41 preserve5conditions and one vector comparison. Activity reload/view metadata traffic and one long back-jump decrease; the cursor store disappears in these callers. Equal adds a limit-register rematerialization. Less_equal hoists a verified0x80 scalar broadcast instead of reading the equivalent16-byte sign mask on each selected iteration.',
        'AVX512 affected one_stage paths36to35/150to137instructionbytes retain4guards, one encoded compare, one existing save/restore and the same musttail completion. kmovw becomes kmovd and a register copy disappears; these counts are not latency or µop estimates.',
        'All3slowAVX512 one_stage candidates place kmovd across a64byte boundary, but w8/op2 has the same boundary pattern and improves A/B0.94096. That counterexample prevents treating the boundary as a causal explanation or justification for an alignment change.',
        'Actual linked AVX512 w5/op2 independent8 is303to298 instructions. The separately retained unlinked witness303to322 used a different complete-caller allocation and cannot substitute for the native ELF.',
        'One AVX512 shared w8/op2 identical-binary duplicate has D/B1.13824 versus A/B1.13962. Other slow rows have duplicate medians near1; the exception neither corrects nor dismisses their measured losses.',
        'No bound/access/contract failure or extra comparison/admission/packet work was found in these selected routes. Known x86 full-tight native losses remain unresolved; linked branch/call-target placement, code/data residence and register scheduling differ, with no PMU or controlled placement experiment here.'
    ]
    return retained


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--summary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compact', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    receipt = json.loads((args.results/'receipt.json').read_text())
    provenance = json.loads((args.summary/'provenance.json').read_text())
    source_manifest = json.loads((args.results/'source-manifest.json').read_text())
    assert receipt['complete'] and receipt['variant'] == 'copied-projection-final'
    assert receipt['candidate_sha256'] == '514e68275e3849bd58549b56f4f78fe81ee48ebce3df4cca830ba4091b568868'
    assert receipt['source_manifest_sha256'] == {
        'before':'929b17889f350d24e2e740c052fc28411fcfaf589261c0216cc9b881b9f2cde6',
        'after':'a4388c9a6695826bec2703d1da8ff1e56062b8c9f81bfeb489b047a2d0ef1d7d'}
    assert receipt['source_manifest_sha256'] == source_manifest['source_manifest_sha256']
    before_map,after_map=(source_manifest['source_sha256'][arm] for arm in ('before','after'))
    filter_header='ikea/include/ikea/tuplepack/filter.h'
    assert before_map.keys()==after_map.keys()
    assert [name for name in before_map if before_map[name]!=after_map[name]] == [filter_header]
    access_headers=['ikea/include/ikea/tuplepack/'+p for p in
                    ('author/native.h','author/execution.h','detail/native/packet.h','detail/native/read.h')]
    assert all(before_map[name]==after_map[name] for name in access_headers)
    assert after_map[access_headers[2]]=='a31462a84217443e95cbe9c3994cb37367ca5843e30a7838bfd35506fa5ed8db'
    assert sha(args.results/'receipt.json') == provenance['sources']['zen']['receipt_sha256']
    assert sha(args.results/'source-manifest.json') == provenance['sources']['zen']['source_manifest_file_sha256']
    result = {'scope': __doc__, 'runner_sha256': sha(Path(__file__)), 'historical_helper_sha256': sha(HELPER),
        'receipt_sha256': sha(args.results/'receipt.json'), 'source_manifest_file_sha256': sha(args.results/'source-manifest.json'),
        'native_provenance_sha256': sha(args.summary/'provenance.json'), 'paired_cases_sha256': sha(args.summary/'paired-cases.csv'),
        'candidate_sha256': receipt['candidate_sha256'], 'source_manifest_sha256': receipt['source_manifest_sha256'],
        'source_epochs':{name:{'before':before_map[name],'after':after_map[name]}
                         for name in [filter_header,*access_headers]},
        'profiles': {}, 'limits': ['Static paths are not a timing model, µop count or causal proof.',
            'Complete symbol-sized function bytes verified against ELF; no alignment padding enters function counts.',
            'Payload equality covers initial allocated bytes at addressed symbols, not mutable runtime state.',
            'Paired native medians and duplicate ratios remain observations; no duplicate correction factor.']}
    for profile in ('avx2','avx512'):
        elfs, arms = {}, {}
        pair = {'inputs': {}, 'functions': {}}
        for arm in ('before','after'):
            folder=args.results/profile/arm
            binary,assembly=folder/'filter_composition_extra',folder/'filters-disassembly.txt'
            want=next(b for b in receipt['binaries'] if (b['profile'],b['arm'],b['family'])==(profile,arm,'filters'))
            assert sha(binary)==want['sha256'] and binary.stat().st_size==want['bytes']
            assert any(b['sha256']==want['sha256'] for b in provenance['sources']['zen']['binaries'])
            elfs[arm]=old.Elf(binary)
            arms[arm]=old.functions(assembly,elfs[arm])
            commands_file=folder/'compile_commands.json'
            commands=[c for c in json.loads(commands_file.read_text()) if 'filter_composition_extra.dir/' in c['command']]
            assert commands and any(c['file'].endswith('/extra_tuple_2.cpp') for c in commands)
            sources={}
            for command in commands:
                flags=shlex.split(command['command'])
                march='x86-64-v3' if profile=='avx2' else 'znver5'
                assert {'-O3','-g','-std=c++23','-fno-vectorize','-fno-slp-vectorize',
                        '-march='+march,'-mtune=znver5','-DFILTER_SEAMS_NO_MAIN=1'} <= set(flags)
                assert not any(f.startswith('-DNDEBUG') for f in flags)
                assert any(f.endswith('/projection-inputs/'+arm+'/ikea/include') for f in flags)
                relative=command['file'].split('/projection-inputs/'+arm+'/',1)[1]
                sources[relative]=source_manifest['source_sha256'][arm][relative]
            pair['inputs'][arm]={'elf_sha256':sha(binary),'bytes':binary.stat().st_size,'disassembly_sha256':sha(assembly),
                'compile_commands_sha256':sha(commands_file),'configured_filter_commands':commands,
                'direct_source_sha256':sources}
        assert arms['before'].keys()==arms['after'].keys() and len(arms['before'])==39
        for key in arms['before']:
            item={}
            for arm in arms:
                f=arms[arm][key]
                item[arm]={k:f[k] for k in ('name','address','size')}
                item[arm].update({'bytes_sha256':hashlib.sha256(elfs[arm].read(f['address'],f['size'])).hexdigest(),
                    'instructions':len(f['rows']),'opcodes':dict(Counter(r.split('\t',1)[0] for _,_,r in f['rows']))})
                if key.endswith(('/bound','/one_stage')):
                    paths={'full_tight_in_domain':walk(f)}
                elif key.endswith('/inline_measure'):
                    paths={mode:inline(f,mode) for mode in ('dependent','independent8')}
                else:
                    paths={}
                for kind,path in paths.items():
                    encoded={a:bytes.fromhex(raw) for a,raw,_ in f['rows']}
                    ranges=[(int(a,16),encoded[int(a,16)]) for a,_ in path['trace']]
                    path['instruction_bytes']=sum(len(raw) for _,raw in ranges)
                    path['occupied_64byte_code_regions']=len({p//64 for a,raw in ranges for p in range(a,a+len(raw))})
                    path['boundary_crossing_instructions']=[
                        {'address':a,'bytes':encoded[int(a,16)].hex(),'instruction':row,
                         'offset_mod64':int(a,16)%64}
                        for a,row in path['trace'] if int(a,16)//64 != (int(a,16)+len(encoded[int(a,16)])-1)//64]
                    path['payloads']=[payload(elfs[arm],int(re.search(r'# 0x([0-9a-f]+)',r)[1],16),r)
                                      for _,r in path['trace'] if '(%rip)' in r]
                    destination=args.output/profile/arm/(key.replace('/','-')+'-'+kind+'.s')
                    destination.parent.mkdir(parents=True,exist_ok=True)
                    destination.write_text('\n'.join(a+': '+r for a,r in path['trace'])+'\n')
                item[arm]['paths']=paths
                item[arm]['broadcast_memory_operands']=[
                    {'instruction_address':hex(a),'instruction':r,
                     'payload':payload(elfs[arm],int(re.search(r'# 0x([0-9a-f]+)',r)[1],16),r)}
                    for a,_,r in f['rows'] if r.split('\t',1)[0].startswith(('vpbroadcast','vbroadcast')) and '(%rip)' in r]
            item['raw_bytes_equal']=item['before']['bytes_sha256']==item['after']['bytes_sha256']
            if key.startswith(('shared/','common/')):
                comparison=compare_layout(arms['before'][key],arms['after'][key],elfs)
                assert comparison['same_relative_offsets'] and comparison['instruction_skeletons_equal'] and comparison['all_addressed_payloads_equal']
                item['layout_comparison']=comparison
            pair['functions'][key]=item
        result['profiles'][profile]=pair
        print(profile,'verified',len(pair['functions']),'complete functions',flush=True)
    (args.output/'receipt.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    if args.compact:
        args.compact.parent.mkdir(parents=True,exist_ok=True)
        args.compact.write_text(json.dumps(compact(result,args.summary,args.output),indent=2,sort_keys=True)+'\n')


if __name__=='__main__':
    main()
