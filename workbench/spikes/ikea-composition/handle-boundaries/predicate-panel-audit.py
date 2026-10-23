#!/usr/bin/env python3
"""Verify the one-image clones against the frozen native op2 consumers.

No compilation or timing. Preserve every instruction/register/immediate except
verified PC-relative locations and the explicit namespace clone correspondence.
Fail before timing if selected complete loops/leaf paths change their work.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('projection_linked', HERE/'projection-linked-filter.py')
linked = importlib.util.module_from_spec(spec)
spec.loader.exec_module(linked)
old = linked.old
ARMS = ('before', 'after', 'duplicate')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def canonical(text):
    for arm in ARMS:
        for name in ('ikea', 'extras', 'runtime'):
            clone = 'panel_'+arm+'_'+name
            # Itanium names carry the length; do not leave a stale length after
            # changing the corresponding source namespace spelling.
            text = text.replace(str(len(clone))+clone, str(len(name))+name)
            text = text.replace(clone, name)
    return text


def role(name):
    for arm in ARMS:
        if 'panel_'+arm+'_' in name:
            key = linked.role(canonical(name))
            return arm+'/'+key if key else None


def parse(path, elf, cloned):
    old.role = role if cloned else linked.role
    return old.functions(path, elf)


def references(elf, function):
    result = {}
    for address, _, row in function['rows']:
        if '(%rip)' not in row:
            continue
        target = int(re.search(r'# 0x([0-9a-f]+)', row)[1], 16)
        section_name, section = elf.section(target)
        if section[2] & 4:
            symbol = re.search(r'# 0x[0-9a-f]+ <(.*)>', row)
            assert symbol, row
            payload = {'kind':'address of executable symbol', 'target':canonical(symbol[1]),
                       'address':hex(target), 'section':section_name}
            identity = {'kind':payload['kind'], 'target':payload['target']}
        else:
            payload = linked.payload(elf, target, row)
            raw = bytes.fromhex(payload['bytes'])
            identity = {'kind':payload['kind'], 'bytes':raw.hex()}
            if payload['kind'] == 'complete nul-terminated string':
                # Only the explicit namespace substitution is admitted for
                # diagnostics. Arbitrary line numbers/literal changes survive.
                identity['bytes'] = canonical(raw.decode()).encode().hex()
        result[address] = {'payload':payload, 'identity':identity}
    return result


def normalized(function, rows, refs):
    selected = {int(a,16) if isinstance(a,str) else a:i for i,(a,row) in enumerate(rows)}
    result = []
    for address, row in rows:
        address = int(address,16) if isinstance(address,str) else address
        body = canonical(row.split('#',1)[0].strip())
        if '(%rip)' in body:
            assert address in refs
            body = re.sub(r'-?0x[0-9a-f]+\(%rip\)', 'PC_RELATIVE(%rip)', body)
            body += ' PAYLOAD='+json.dumps(refs[address]['identity'],sort_keys=True)
        if re.match(r'^(?:j\w*|callq)\t0x', body):
            target = int(re.search(r'0x([0-9a-f]+) <', body)[1],16)
            if target in selected:
                destination = 'ROW:'+str(selected[target])
            elif function['address'] <= target < function['address']+function['size']:
                destination = 'FUNCTION_OFFSET:'+str(target-function['address'])
            else:
                symbol = re.search(r'0x[0-9a-f]+ <(.*)>', body)
                assert symbol, body
                destination = 'SYMBOL:'+canonical(symbol[1])
            body = re.sub(r'0x[0-9a-f]+ <.*>', destination, body, count=1)
        result.append(body)
    return result


def compare(left, right):
    return {'equal':left == right, 'left_instructions':len(left), 'right_instructions':len(right),
            'left_sha256':digest(left), 'right_sha256':digest(right),
            'differences':[[i,a,b] for i,(a,b) in enumerate(zip(left,right)) if a!=b],
            'unpaired_left':left[len(right):], 'unpaired_right':right[len(left):]}


def paths(function):
    key = linked.role(canonical(function['name']))
    if key.endswith(('/bound','/one_stage')):
        return {'full_tight_in_domain':linked.walk(function)}
    if key.endswith('/inline_measure'):
        return {mode:linked.inline(function,mode) for mode in ('dependent','independent8')}
    return {}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    manifest=json.loads((args.inputs/'manifest.json').read_text())
    build=json.loads((args.output/'build-receipt.json').read_text())
    assert build['complete'] and build['manifest_file_sha256']==sha(args.inputs/'manifest.json')
    result={'scope':__doc__, 'complete':False, 'runner_sha256':sha(Path(__file__)),
            'manifest_sha256':sha(args.inputs/'manifest.json'), 'build_receipt_sha256':sha(args.output/'build-receipt.json'),
            'limits':['Source namespace clones give separately linked code and state in one process. They do not make addresses equal.',
                      'All symbol-sized selected bodies are verified against ELF bytes; selected path equality excludes only explicit namespace names and verified PC-relative locations.',
                      'Equal selected instructions/payloads do not imply equal fetch, prediction, cache residency or time.',
                      'Whole-function differences outside the selected paths are retained rather than normalized away.'], 'profiles':{}}
    failures=[]
    for profile in build['profiles']:
        folder=args.output/profile
        binary=folder/'predicate-panel'
        assert sha(binary)==build['binaries'][profile]['sha256']
        elf=old.Elf(binary)
        combined=parse(folder/'panel-disassembly.txt',elf,True)
        keys={arm:{k.split('/',1)[1]:v for k,v in combined.items() if k.startswith(arm+'/')} for arm in ARMS}
        assert keys['before'].keys()==keys['after'].keys()==keys['duplicate'].keys()
        assert len(keys['before'])==11, sorted(keys['before'])
        pair={'elf_sha256':sha(binary), 'functions':{}, 'reference_binaries':{}}
        originals={};original_elfs={}
        for arm in ('before','after'):
            relative=str(Path('references')/profile/arm/'filter_composition_extra')
            reference=args.inputs/relative
            assert sha(reference)==manifest['reference_binaries'][relative]['sha256']
            assembly=folder/('reference-'+arm+'-disassembly.txt')
            with assembly.open('w') as output:
                subprocess.run(['llvm-objdump-21','-d','-r','--demangle',str(reference)],stdout=output,check=True)
            original_elfs[arm]=old.Elf(reference)
            originals[arm]=parse(assembly,original_elfs[arm],False)
            pair['reference_binaries'][arm]={'sha256':sha(reference),'assembly_sha256':sha(assembly)}
        for key in keys['before']:
            item={'arms':{}, 'duplicate_complete_function':{}}
            normal={};all_paths={}
            for arm in ARMS:
                f=keys[arm][key];refs=references(elf,f)
                normal[arm]=normalized(f,[(a,row) for a,_,row in f['rows']],refs)
                all_paths[arm]=paths(f)
                info={'name':f['name'],'address':hex(f['address']),'size':f['size'],
                      'bytes_sha256':hashlib.sha256(elf.read(f['address'],f['size'])).hexdigest(),
                      'instructions':len(f['rows']), 'normalized_sha256':digest(normal[arm]),
                      'addressed_payloads':list(refs.values()), 'paths':{}, 'reference_complete_function':{}}
                reference_arm='before' if arm=='duplicate' else arm
                original=originals[reference_arm][key]
                original_refs=references(original_elfs[reference_arm],original)
                info['reference_complete_function']=compare(normalized(original,[(a,row) for a,_,row in original['rows']],original_refs),normal[arm])
                original_paths=paths(original)
                assert all_paths[arm].keys()==original_paths.keys()
                for kind,path in all_paths[arm].items():
                    comparison=compare(normalized(original,original_paths[kind]['trace'],original_refs),normalized(f,path['trace'],refs))
                    info['paths'][kind]={'instructions':path['instructions'],'conditions':path['conditions'],
                                         'trace_sha256':path['trace_sha256'],'reference_comparison':comparison}
                    if not comparison['equal']:
                        failures.append([profile,arm,key,kind,'changed selected work vs original native consumer'])
                    destination=folder/arm/(key.replace('/','-')+'-'+kind+'.s')
                    destination.write_text('\n'.join(a+': '+row for a,row in path['trace'])+'\n')
                # Shared execution and common handoff are complete-function
                # controls, not just the conveniently selected predicate path.
                if key.startswith(('shared/','common/')) and not info['reference_complete_function']['equal']:
                    failures.append([profile,arm,key,'changed complete shared/control work vs reference'])
                item['arms'][arm]=info
            duplicate=compare(normal['before'],normal['duplicate'])
            item['duplicate_complete_function']=duplicate
            if not duplicate['equal']:
                failures.append([profile,key,'before/duplicate complete-function work differs'])
            assert keys['before'][key]['address'] != keys['duplicate'][key]['address'],key
            pair['functions'][key]=item
        result['profiles'][profile]=pair
    result['failures']=failures
    result['complete']=not failures
    (args.output/'identity.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    assert not failures, failures
    print('one-image selected consumer identity verified',build['profiles'],flush=True)


if __name__=='__main__':
    main()
