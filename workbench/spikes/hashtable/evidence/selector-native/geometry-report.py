#!/usr/bin/env python3
"""Retain all geometry trade-offs; no per-cell winner becomes a selected policy."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path
import random
from statistics import median

p=argparse.ArgumentParser()
p.add_argument('directory',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--verify-binaries',action='store_true')
a=p.parse_args()
receipt=json.loads((a.directory/'processes.json').read_text())
keys,widths,passes,reps=(receipt[f] for f in ('keys','widths','passes','reps'))
variants=receipt.get('variants',['geometry'])
assert variants in (['geometry'],['composed16','direct16','composed32','direct32'])
def identity(v,k,w):return f'{k}-{w}' if v=='geometry' else f'{v}-{k}-{w}'
def name(v,k,w):return f'geometry-{k}-{w}' if v=='geometry' else f'selector-{v}-{k}-{w}'
shapes=[(0,128),(-1,128),(-2,128),(1,128),(0,96),(-1,96),(0,192),(-1,192),(0,64),(1,64),
        (-2,96),(1,96),(-2,192),(1,192),(-1,64),(-2,64)]
baseline=len(shapes)
phases=['construct','build','hit','miss','serial','update','erase','release','lifecycle']
schedule=[(v,k,w,arm,large) for v in variants for k in keys for w in widths for arm in range(baseline+2) for large in (0,1)]
random.Random(991).shuffle(schedule)
full=[(s,*case) for s in range(passes) for case in (schedule if s%2==0 else reversed(schedule))]
rows=list(csv.DictReader((a.directory/'values.csv').open()))
assert len(receipt['processes'])==len(full) and len(rows)==len(full)*reps*9
groups=defaultdict(list);verified=set()
for order,((s,v,k,w,arm,large),process) in enumerate(zip(full,receipt['processes'])):
    assert process['returncode']==0 and process['ended_unix']>=process['started_unix']
    if order:assert process['started_unix']>=receipt['processes'][order-1]['ended_unix']
    argv=process['argv']
    assert argv[:2]==['taskset','-c'] and list(map(int,argv[-5:]))==[int(w!=0),arm,1,large,reps]
    if a.verify_binaries and (v,k,w) not in verified:
        binary=a.directory/name(v,k,w)
        if not binary.exists():binary=Path(argv[3])
        assert hashlib.sha256(binary.read_bytes()).hexdigest()==receipt['binary_sha256'][identity(v,k,w)]
        verified.add((v,k,w))
    reference=10 if large else 6;n=6<<reference
    shift,unit=shapes[arm] if arm<baseline else (0,0)
    level=reference+shift
    # Independent exact byte-plane fit: 5/9-byte headers, byte fingerprints,
    # rounded suffix bytes and W value bytes, at most 32 slots per bucket.
    def field_bytes(bits):return 1+(max(0,k-bits-8)+7)//8+w
    def slots(bits,header):return min(32,(unit-header)//field_bytes(bits))
    for i,row in enumerate(rows[order*reps*9:(order+1)*reps*9]):
        if v!='geometry':assert row['selector_variant']==v
        assert tuple(int(row[f]) for f in ('K','value_bytes','kind','arm','fanout','reference_A','A','n','rep','pass_index','process_order'))==(
            k,w,int(w!=0),arm,1,reference,level,n,i//9,s,order)
        phase='ordered' if w==0 and i%9==4 else phases[i%9];assert row['phase']==phase
        operations=1 if phase in ('construct','release') else n//2 if phase=='erase' else n*max(8,65536//n) if phase in ('hit','miss','serial','ordered') else n
        assert int(row['operations'])==operations
        assert int(row['inline_values'])==0 and int(row['abseil_inline_capacity'])==0
        assert tuple(int(row[f]) for f in ('B','bucket_bytes','primary_slots','secondary_slots'))==(
            (level-4,unit,slots(level,5),slots(level-4,9)) if arm<baseline else (0,0,0,0))
        population=[int(row['build_'+tier+'_rows']) for tier in ('primary','secondary','overflow')]
        assert sum(population)==(n if arm<baseline else 0)
        if arm<baseline:
            assert population[0]<=slots(level,5)*(1<<level)
            assert population[1]<=slots(level-4,9)*(1<<(level-4))
        for tier,bits,header in (('primary',level,5),('secondary',level-4,9)):
            used=header+slots(bits,header)*field_bytes(bits)
            assert (int(row[tier+'_used']),int(row[tier+'_padding']))==((used,unit-used) if arm<baseline else (0,0))
        groups[v,k,w,reference,arm,phase].append(row)

def ns(batch):return median(float(r['ns'])/int(r['operations']) for r in batch)
def logical(batch):return median(int(r['requested'])+int(r['metadata']) for r in batch)
comparison=[]
for key,batch in sorted(groups.items()):
    v,k,w,reference,arm,phase=key
    ordinary=groups[v,k,w,reference,baseline,phase];exact=groups[v,k,w,reference,baseline+1,phase];control=groups[v,k,w,reference,0,phase]
    assert len(batch)==passes*reps
    entry=dict(K=k,value_bytes=w,reference_A=reference,arm=arm,phase=phase,
        ns_per_operation=ns(batch),over_abseil=ns(batch)/ns(ordinary),over_exact_abseil=ns(batch)/ns(exact),over_control=ns(batch)/ns(control),
        logical_bytes=logical(batch),logical_over_abseil=logical(batch)/logical(ordinary),
        owned_capacity_min=min(int(r['owned_capacity']) for r in batch),owned_capacity_max=max(int(r['owned_capacity']) for r in batch))
    for f in ('A','B','n','bucket_bytes','primary_slots','secondary_slots','primary_used','primary_padding','secondary_used','secondary_padding',
              'build_primary_rows','build_secondary_rows','build_overflow_rows'):
        values={int(r[f]) for r in batch};assert len(values)==1;entry[f]=values.pop()
    for s in range(passes):entry[f'pass_{s}_ns']=ns([r for r in batch if int(r['pass_index'])==s])
    if v!='geometry':
        entry['selector_variant']=v
        for other in variants:
            peer=groups[other,k,w,reference,arm,phase]
            # Execution substitution cannot silently change the representation.
            assert logical(batch)==logical(peer)
            for f in ('A','B','n','bucket_bytes','primary_slots','secondary_slots','primary_used','primary_padding','secondary_used','secondary_padding',
                      'build_primary_rows','build_secondary_rows','build_overflow_rows'):
                assert {int(r[f]) for r in batch}=={int(r[f]) for r in peer}
            entry[f'over_{other}']=ns(batch)/ns(peer)
    comparison.append(entry)
a.output.write_text(json.dumps(dict(rows=len(rows),processes=len(full),comparison=comparison),indent=2)+'\n')
with a.output.with_suffix('.csv').open('w') as out:
    writer=csv.DictWriter(out,fieldnames=list(comparison[0]));writer.writeheader();writer.writerows(comparison)
print(f'Validated {len(rows)} rows and {len(full)} fresh processes; complete geometry inventories match')
print('K/W/reference/arm/phase: ns, /Abseil time, /Abseil logical memory; >1 is worse')
for r in comparison:
    if r['reference_A']==10 and r['phase'] in ('hit','serial','ordered','update','lifecycle'):
        print(r.get('selector_variant','geometry'),*(r[f] for f in ('K','value_bytes','reference_A','arm','phase')),
              *(f'{r[f]:.3f}' for f in ('ns_per_operation','over_abseil','logical_over_abseil')))
