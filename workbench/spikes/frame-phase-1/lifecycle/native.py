#!/usr/bin/env python3
"""Run the existing phase programs on a native host; no new workload semantics."""
from pathlib import Path
import argparse,csv,hashlib,json,os,platform,shutil,statistics,subprocess,time

p=argparse.ArgumentParser()
p.add_argument('--build',type=Path,required=True);p.add_argument('--inputs',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--cpu',type=int,required=True)
p.add_argument('--isa',required=True);a=p.parse_args()
root=Path.cwd();build=a.build.resolve();out=a.output.resolve();out.mkdir(exist_ok=True,parents=True)
inputs=a.inputs.resolve();binary=build/'workbench/spikes/frame-phase-1/lifecycle/frame_phase_measure'
reference=json.loads((Path(__file__).parent/'native-inputs.json').read_text())
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
for item in reference['inputs']:
    path=inputs/Path(item['file']).name
    assert path.stat().st_size==8192*8 and sha(path)==item['sha256'],path
identity={'isa':a.isa,'cpu':a.cpu,'platform':platform.platform(),'affinity':sorted(os.sched_getaffinity(0)),
          'source_commit':os.environ['SIXDB_SOURCE_COMMIT'],
          'commands':[],'inputs':reference,'conditions':'Warm fixed2x4096-row owner histories; no cold/DRAM/Loom claim.',
          'trials':'20 arms; bounded40 and full20 balanced samples/cell; exact duplicate arms1/10,4/11,13/15,16/17.'}
identity['compile_commands']=[v for v in json.loads((build/'compile_commands.json').read_text())
    if '/frame-phase-1/lifecycle/phase_' in v['file'] or '/frame-phase-1/encoding/' in v['file']]
executables=[binary]+[binary.parent/v for v in ['frame_phase_material_check','frame_phase_repair_check','frame_phase_batch_check']]
executables+=[build/'workbench/spikes/frame-phase-1/frame_measure_order_check']
(out/'binaries').mkdir(exist_ok=True);identity['executables']={}
for exe in executables:
    identity['executables'][exe.name]=sha(exe);shutil.copy2(exe,out/'binaries'/exe.name)
def invoke(command,name):
    identity['commands'].append(command)
    with (out/name).open('w') as f:subprocess.run(command,stdout=f,check=True)
    return (out/name).read_text()
checks={};start=time.monotonic()
for exe in executables[1:]:checks[exe.name]=invoke([str(exe)],exe.name+'.txt').strip()
for name in ['--check-inline-repair','--check-nullable-fold','--check-batch-overrides','--check-native-coalesced']:
    checks[name]=invoke([str(binary),name],name.removeprefix('--')+'.txt').strip()
rows=[];original_points=0;accounting=0
for item in reference['inputs']:
    inp=inputs/Path(item['file']).name;label=inp.stem
    # Bounded original-coordinate verification is separate from timings.
    command=['taskset','-c',str(a.cpu),str(binary),str(inp),'8','1','1','1','mixed','bounded','masked']
    data=[json.loads(v) for v in invoke(command,label+'-verify.jsonl').splitlines()]
    assert len(data)==800 and all(v['verification_run']==1 for v in data)
    original_points+=sum(8192*9 for v in data if v['trial']==0)
    for trace in ['mixed','values-only']:
        command=['taskset','-c',str(a.cpu),str(binary),str(inp),'8','1','0','0',trace,'bounded','masked','capacity']
        data=[json.loads(v) for v in invoke(command,label+'-'+trace+'-capacity.jsonl').splitlines()]
        assert len(data)==800 and all(v['capacity_accounting']==1 for v in data);accounting+=len(data)
        for mix,queries,final in [('update',1,0),('read-rich',64,512)]:
            command=['taskset','-c',str(a.cpu),str(binary),str(inp),'24',str(queries),str(final),'0',trace,'bounded','masked','plain']
            data=[json.loads(v) for v in invoke(command,label+'-'+trace+'-'+mix+'.jsonl').splitlines()]
            assert len(data)==1600
            for row in data:
                assert row['capacity_accounting']==0 and row['publications']==8
                row.update(input=label,trace=trace,coverage='bounded',mix=mix);rows.append(row)
    print(a.isa,label,'bounded checks and16 programs/duplicates complete',flush=True)
for name in ['p__mbar','UserID']:
    command=['taskset','-c',str(a.cpu),str(binary),str(inputs/(name+'.u64')),'16','1','0','0','mixed']
    data=[json.loads(v) for v in invoke(command,name+'-full-update.jsonl').splitlines()]
    assert len(data)==12800
    for row in data:
        assert row['capacity_accounting']==0
        row.update(input=name,trace='mixed',coverage='full',mix='update');rows.append(row)
groups={};keys=['input','trace','coverage','mix','pattern','mask','retain_old','policy','arm']
for row in rows:groups.setdefault(tuple(row[k] for k in keys),[]).append(row)
summary=[]
for key,rs in sorted(groups.items()):
    row=dict(zip(keys,key));row.update(samples=len(rs),capacity_accounting=0)
    for field in ['total_ns','prepare_ns','publish_ns','query_ns','conversion_ns','release_ns','copied_bytes','encoded_rows',
                  'repaired_blocks','write_materialized_rows','repair_examined_rows','eligible_dirty_blocks','coalesced_away',
                  'visible_noops','publications','max_update_ns','max_transition_ns','max_release_ns','peak_metadata_bytes','final_nonidle_bytes']:
        row[field]=statistics.median(v[field] for v in rs)
    for field in ['dirty_kinds','repair_reasons']:
        assert all(v[field]==rs[0][field] for v in rs);row[field]=rs[0][field]
    row['samples_ns']=[v['total_ns'] for v in rs];summary.append(row)
for exe in executables:assert sha(exe)==identity['executables'][exe.name]
identity['elapsed_seconds']=time.monotonic()-start
checks.update(timed_histories=len(rows),accounting_histories=accounting,original_point_checks=original_points)
(out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
(out/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
with (out/'cells.csv').open('w',newline='') as f:
    fields=[k for k in summary[0] if k!='samples_ns'];w=csv.DictWriter(f,fields,lineterminator='\n');w.writeheader()
    for row in summary:w.writerow({k:json.dumps(row[k]) if isinstance(row[k],list) else row[k] for k in fields})
print(a.isa,'complete',len(rows),'timed histories;',original_points,'original-coordinate point checks',flush=True)
