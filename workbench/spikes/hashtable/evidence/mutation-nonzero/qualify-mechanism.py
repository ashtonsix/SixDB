#!/usr/bin/env python3
"""Check scoped instruction witnesses in the already-built local binaries.

Run audit-dwarf.py first; this script does not compile or time anything.
The MM address ranges intentionally describe only these SHA-qualified binaries.
"""
import csv
import difflib
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

root=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt=json.loads((root/'receipt.json').read_text())
pattern=re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s+(.*)$')
def sequence(path):
    return [(int(m[1],16),m[2],m[3]) for line in path.read_text().splitlines() if (m:=pattern.match(line))]
def target(text):return int(re.search(r'0x([0-9a-f]+)',text)[1],16)
def bare(instruction):
    a,o,t=instruction
    return f'{a:x}: {o} {t.split(" <",1)[0]}'

result={'scope':'Local AArch64 K40/W2 complete-operation binaries; instruction-level mechanism only, no timing or native-host conclusion.',
        'map_assumption':'Successful existing primary match; one iteration followed until the next-key loop boundary.',
        'mm_assumption':'A10 arm0 primary inline record with tag 0, 2 or 3, so the W2/B8 append does not allocate.',
        'variants':{},'map_paths':[],'mm_paths':[]}
excerpts=[]
for variant in ('partial','nonzero'):
    path=root/variant
    command=next(c for c in receipt['commands'] if c['variant']==variant and c['kind']=='bench')
    for name,key in [('values-2','binary_sha256'),('value_width_bench.cpp','source_sha256'),('promoted.h','promoted_sha256'),('record_table.h','record_table_sha256')]:
        assert sha(path/name)==command[key],(variant,name)
    codegen=json.loads((path/'audit/codegen.json').read_text())
    assert not codegen['failures'] and len(codegen['timed_roots'])==20
    assert codegen['binary_sha256']==command['binary_sha256']
    assert '-fno-vectorize' in command['compile'] and '-fno-slp-vectorize' in command['compile']
    result['variants'][variant]={k:command[k] for k in ('binary_sha256','source_sha256','promoted_sha256','record_table_sha256')}
    result['variants'][variant].update(assembly_sha256=sha(path/'assembly.gz'),codegen_sha256=sha(path/'audit/codegen.json'),
        update_roots=20,audit_failures=0,isa='armv8.2-a+crypto',tune='generic',compiler=command['compile'][0],
        optimization='-O3',autovectorization=False)
    for bits in (6,10):
        for arm in range(3):
            file=path/'audit'/f'K40-W2-kind1-A{bits}-arm{arm}-update-helper.asm'
            seq=sequence(file);positions={a:i for i,(a,_,_) in enumerate(seq)}
            loop=target(next(t for _,o,t in seq if o=='b'))
            # First fingerprint candidate extraction, followed by the suffix
            # equality branch. Its true edge is the existing primary hit.
            first=next(i for i,(_,o,_) in enumerate(seq) if o=='clz')
            compare=next(i for i in range(first,len(seq)) if seq[i][1]=='cmp')
            assert seq[compare+1][1]=='b.eq'
            at=target(seq[compare+1][2]);trace=[]
            for _ in range(100):
                if at==loop:break
                i=positions[at];a,o,t=seq[i];trace.append(seq[i]);following=seq[i+1][0]
                if o=='b':at=target(t);continue
                if o.startswith('b.'):
                    assert target(t)==loop or following==loop,(variant,bits,arm,hex(a))
                    break
                assert o not in ('cbz','cbnz','tbz','tbnz','bl','blr')
                at=following
            else:raise AssertionError('unbounded primary-hit trace')
            owner=re.search(r'\[(x\d+), #0x58\]',file.read_text())[1]
            reads=[bare(i) for i in trace if i[1].startswith('ld') and any(f'[{owner}, #{off}]' in i[2] for off in ('0x118','0x120'))]
            stores=[bare(i) for i in trace if i[1].startswith('st') and any(f'[{owner}, #{off}]' in i[2] for off in ('0x118','0x120'))]
            assert len(reads)==len(stores)==(2 if variant=='partial' else 0)
            item={'variant':variant,'A':bits,'arm':arm,'source':str(file.relative_to(root)),
                  'match_branch':hex(seq[compare+1][0]),'entry':hex(trace[0][0]),'last':hex(trace[-1][0]),
                  'loop':hex(loop),'owner_register':owner,'counter_reads':reads,'counter_stores':stores}
            result['map_paths'].append(item)
            excerpts.append(f'\n{variant}: map K40/W2 A{bits} arm{arm}; successful primary-match edge at {item["match_branch"]}\n'+
                '\n'.join(bare(i) for i in trace)+'\n')

mm_ranges={
    'partial':[(0x4b6ac,0x4b6cc),(0x4b750,0x4b78c),(0x4b914,0x4b91c),(0x4b28c,0x4b2b0)],
    'nonzero':[(0x4b744,0x4b764),(0x4b7e8,0x4b828),(0x4b330,0x4b348)]}
for variant,ranges in mm_ranges.items():
    file=root/variant/'audit/K40-W2-kind2-A10-arm0-update-helper.asm'
    seq=sequence(file);by={a:(a,o,t) for a,o,t in seq}
    trace=[]
    for first,last in ranges:
        assert first in by and last in by
        trace.extend(by[a] for a in range(first,last+4,4))
    assert any(i[1]=='mov' and i[2].startswith('w14, #0x1 ') for i in trace)
    value_stores=[bare(i) for i in trace if i[1].startswith('st') and '[x27, #0x128]' in i[2]]
    key_stores=[bare(i) for i in trace if i[1].startswith('st') and '[x27, #0x120]' in i[2]]
    assert len(value_stores)==1 and len(key_stores)==(1 if variant=='partial' else 0)
    result['mm_paths'].append({'variant':variant,'A':10,'arm':0,'source':str(file.relative_to(root)),
        'path_ranges':[[hex(a),hex(b)] for a,b in ranges], 'value_delta':1,
        'value_counter_stores':value_stores,'key_counter_stores':key_stores,
        'path_decisions':'Take inline eligibility; primary tier=1; skip secondary and overflow branches; follow final unconditional branch to owner accounting.'})
    excerpts.append(f'\n{variant}: MM K40/W2 A10 arm0; primary inline append, tag 0/2/3\n'+
        '\n'.join(bare(i) for i in trace)+'\n')

rows=list(csv.DictReader((root/'results/mutation.csv').open()))
assert len(rows)==10800
inventory=defaultdict(set);population=defaultdict(set);coverage=Counter()
for row in rows:
    key=tuple(row[k] for k in ('K','value_bytes','kind','A','arm','fanout','phase'))
    physical=tuple(row[k] for k in ('requested','owned_capacity','metadata','inline_values','abseil_inline_capacity',
                                  'build_primary_rows','build_secondary_rows','build_overflow_rows'))
    inventory[key].add(physical);coverage[row['variant']]+=1
assert all(len(v)==1 for v in inventory.values())
processes=json.loads((root/'results/processes.json').read_text())
assert all(p['returncode']==0 and not p['stderr'] for p in processes['processes'])
for variant in ('partial','nonzero'):
    expected=result['variants'][variant]['binary_sha256']
    matches=[h for p,h in processes['binary_sha256'].items() if p.endswith('/'+variant+'/values-2')]
    assert matches==[expected]
result['local_context']={'rows':len(rows),'rows_by_variant':dict(coverage),'successful_processes':len(processes['processes']),
    'matched_inventory_cells':len(inventory),'schedule_seed':processes['schedule_seed'],
    'cpu_affinity':sorted({p['argv'][2] for p in processes['processes']}),
    'warning':'Unchanged Abseil arms vary between local runs. This receipt makes no causal performance estimate or platform-selection claim.',
    'raw_csv_sha256':sha(root/'results/mutation.csv'),'processes_sha256':sha(root/'results/processes.json')}
delta=''.join(difflib.unified_diff((root/'partial/promoted.h').read_text().splitlines(True),
    (root/'nonzero/promoted.h').read_text().splitlines(True),fromfile='partial/promoted.h',tofile='nonzero/promoted.h'))
assert delta.count('\n-        if(r)')==delta.count('\n+        if(r)')==1
(root/'source-delta.txt').write_text(delta)
(root/'witnesses.txt').write_text('Instruction excerpts from already-built AArch64 binaries.\nNo instruction count is interpreted as operation latency.\n'+''.join(excerpts))
(root/'mechanism.json').write_text(json.dumps(result,indent=2)+'\n')
print('Verified 12 map primary-hit traces, 2 MM inline traces, 40 update roots and 10,800 matched-inventory rows.')
