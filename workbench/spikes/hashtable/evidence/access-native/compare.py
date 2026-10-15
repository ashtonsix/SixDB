#!/usr/bin/env python3
"""Regenerate the finite access comparison from retained measurements and audit.
Usage: compare.py EVIDENCE_DIR CODEGEN_JSON OUTPUT_DIR
All medians are descriptive; no significance or cache-residence inference.
"""
import csv, json, re, statistics, sys
from pathlib import Path
import report
import geometry

source, audit_path, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
rows = report.summarize(source/'formats.csv', True, True)
assert len(rows) == 4050 and all(r['repetitions'] == 9 for r in rows)
lookup = {(r['K'],r['kind'],r['A'],r['arm'],r['phase']):r for r in rows}

def write_csv(name, data):
    columns = list(dict.fromkeys(k for r in data for k in r))
    with (out/name).open('w') as f:
        writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader();writer.writerows(data)

cases=[]
for key in sorted({(r['K'],r['kind'],r['A'],r['arm']) for r in rows}):
    k,kind,a,arm=key
    initial=lookup[(*key,'hit')]
    row={name:initial[name] for name in ['K','kind','A','arm','name','n','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']}
    if arm<8:
        physical=geometry.case(k,kind,a,1)
        assert [physical[t]['slots'] for t in ['primary','secondary']]==[row['primary_slots'],row['secondary_slots']]
        row['permission_changes_bytes']=int(geometry.access_case(k,kind,a,arm//2)!=geometry.access_case(k,kind,a,0))
        for phase in ['hit','update','erase']:
            cell=lookup[(*key,phase)];control=lookup[(k,kind,a,0,phase)]
            for name in ['n','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows','requested','metadata']:
                assert cell[name]==control[name],(key,phase,name)
    for phase in ['construct','build','hit','miss','serial_caller_cycle' if kind==0 else 'serial_value_cycle','update','erase','release','lifecycle']:
        cell=lookup[(*key,phase)];short='serial' if phase.startswith('serial') else phase
        for field in ['ns_per_operation','minimum','maximum','operations','ratio_to_abseil','ratio_to_abseil_exact']:
            row[short+'_'+field]=cell[field]
        if arm<8:
            control=lookup[(k,kind,a,arm%2,phase)]
            row[short+'_ratio_to_same_codec_exact']=cell['ns_per_operation']/control['ns_per_operation']
    for phase,name in [('hit','initial'),('update','updated'),('erase','erased')]:
        cell=lookup[(*key,phase)]
        for field in ['requested','owned_capacity','metadata']:row[name+'_'+field]=cell[field]
        row[name+'_logical_bytes']=cell['requested']+cell['metadata']
    cases.append(row)
write_csv('comparison.csv',cases)

ranges=[]
for arm in range(8):
    for phase in ['hit','miss','serial_value_cycle','serial_caller_cycle','update','erase','lifecycle']:
        for baseline in ['abseil','abseil_exact','same_codec_exact']:
            cells=[r for r in rows if r['arm']==arm and r['phase']==phase]
            values=[]
            for r in cells:
                if baseline=='same_codec_exact':
                    ref=lookup[(r['K'],r['kind'],r['A'],arm%2,phase)]['ns_per_operation']
                    values.append(r['ns_per_operation']/ref)
                else:values.append(r['ratio_to_'+baseline])
            ranges.append(dict(arm=arm,name=report.ACCESS_NAMES[arm],phase=phase,baseline=baseline,cases=len(values),minimum=min(values),median=statistics.median(values),maximum=max(values),at_or_below_one=sum(v<=1 for v in values)))
write_csv('ranges.csv',ranges)

audit=json.loads(audit_path.read_text());assert not audit['failures'];assert len(audit['timed_roots'])==1800
callees={r['address']:r for r in audit['direct_callees']}
def address(call):return re.match(r'(0x[0-9a-f]+)',call['target'])[1]
def classify(call):
    t=call['target']
    if 'format_trial<' in t:return 'whole_read_loop'
    if any(x in t for x in ['__cxa_guard_acquire','__cxa_guard_release','__cxa_atexit','::formats()']):return 'cold_format_init'
    if any(x in t for x in ['__cxa_guard_abort','_Unwind','__clang_call_terminate','__cxa_throw']):return 'failure_path'
    if 'record_table<' in t and '::erase<' in t:return 'record_erase'
    if 'EraseMetaOnly' in t:return 'baseline_erase_metadata'
    if 'operator delete' in t:return 'deallocation'
    return t.split(' <',1)[-1].removesuffix('>')
code=[];read_helpers={};inline_candidate_reads=0
for r in audit['timed_roots']:
    assert not r['unknown_branches']
    assert not any(c['indirect'] for c in r['calls'])
    entry={k:r[k] for k in ['K','kind','A','arm','phase','clock','instructions']}
    entry.update(end='|'.join(r['end']),calls='|'.join(classify(c)+'@'+address(c) for c in r['calls']))
    if r['phase']!='erase':
        if r['arm']>=8:assert not r['calls']
        else:
            helpers=[c for c in r['calls'] if classify(c)=='whole_read_loop']
            if helpers:
                assert len(r['calls'])==len(helpers)==1
                sub=callees[address(helpers[0])]
                assert all(classify(c) in ['cold_format_init','failure_path'] for c in sub['calls'])
                entry.update(helper_address=sub['address'],helper_normalized_sha256=sub['normalized_sha256'],helper_instructions=sub['instructions'],helper_call_kinds='|'.join(sorted({classify(c) for c in sub['calls']})))
                read_helpers[(r['K'],r['kind'],r['A'],r['arm'],r['phase'])]=sub
            else:
                assert all(classify(c) in ['cold_format_init','failure_path'] for c in r['calls'])
                inline_candidate_reads+=1
    code.append(entry)
write_csv('codegen.csv',code)

identical=[]
for (k,kind,a,arm,phase),sub in read_helpers.items():
    if arm<2:continue
    control=read_helpers.get((k,kind,a,arm%2,phase))
    if control is None or control['normalized_sha256']!=sub['normalized_sha256']:continue
    actual_phase=('serial_caller_cycle' if kind==0 else 'serial_value_cycle') if phase=='serial' else phase
    value=lookup[(k,kind,a,arm,actual_phase)]['ns_per_operation'];ref=lookup[(k,kind,a,arm%2,actual_phase)]['ns_per_operation']
    identical.append(dict(K=k,kind=kind,A=a,arm=arm,phase=phase,control_arm=arm%2,ratio=value/ref,ns_per_operation=value,control_ns_per_operation=ref,helper_address=sub['address'],control_address=control['address'],normalized_sha256=sub['normalized_sha256']))
write_csv('identical-code-controls.csv',identical)
result=dict(timed_regions=len(code),read_regions=1350,baseline_read_regions_without_calls=270,candidate_read_regions=1080,candidate_whole_loop_helpers=len(read_helpers),candidate_inlined_read_regions=inline_candidate_reads,unknown_branches=0,indirect_calls=0,assembly_sha256=audit['assembly_sha256'],identical_helper_comparisons=len(identical),identical_helper_ratio_min=min(r['ratio'] for r in identical),identical_helper_ratio_max=max(r['ratio'] for r in identical))
(out/'audit-summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
