#!/usr/bin/env python3
"""Retain complete controls and independently audit the executed schedule."""
from pathlib import Path
from collections import Counter,defaultdict
import argparse,csv,hashlib,json,shutil

p=argparse.ArgumentParser();p.add_argument('output',type=Path)
p.add_argument('--cohort',action='append',required=True,metavar='NAME,JOB,CAPTURE')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
def read(path):return json.loads(path.read_text())
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def csv_file(path,rows):
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)

for item in a.cohort:
    name,job,capture=item.split(',');source=Path('build/workers')/job
    dest=a.output/name;dest.mkdir();full=dest/'full';selected=dest/'selected';selected.mkdir()
    shutil.copytree(source/'results/avx512',full)
    for file in ['job.json','status.json','assignment.json','collection.json','source-manifest.json','source.tar.gz']:
        shutil.copy2(source/file,dest/file)
    for file in ['lscpu.json','cache.txt','compiler.txt','python-packages.txt','input-artifact.json','status.txt']:
        shutil.copy2(source/'results'/file,dest/file)
    receipt=Path(capture+'.capture.json');shutil.copy2(receipt,dest/'capture.json')
    wanted=read(receipt)['source_files_sha256'];actual=read(source/'source-manifest.json')
    assert wanted==actual,'worker source differs from reviewed capture'
    q=full/'queries';identity=read(q/'identity.json')
    assert sha(full/'binaries/libframe_scan_bridge.so')==identity['library_sha256']
    assert read(source/'status.json')['state']=='complete'
    assert 'OK' in (full/'after.txt').read_text()
    rows=[json.loads(line) for line in (q/'samples.jsonl').read_text().splitlines()]
    groups=defaultdict(list);keys=['input','rows','layout','operation','selection','condition']
    for row in rows:groups[tuple(row[k] for k in keys)].append(row)
    for key,rs in groups.items():
        arms={r['arm'] for r in rs};m=len(arms);assert m%2==0
        trials={r['trial'] for r in rs};assert len(trials)%m==0
        cycles=len(trials)//m;positions=Counter();pairs=Counter()
        assert len(rs)==m*len(trials) and len({r['answer'] for r in rs})==1
        for trial in trials:
            ordered=sorted((r for r in rs if r['trial']==trial),key=lambda r:r['position'])
            assert [r['position'] for r in ordered]==list(range(m))
            assert {r['arm'] for r in ordered}==arms
            for r in ordered:positions[r['arm'],r['position']]+=1
            for left,right in zip(ordered,ordered[1:]):pairs[left['arm'],right['arm']]+=1
        assert all(positions[arm,pos]==cycles for arm in arms for pos in range(m))
        assert all(pairs[left,right]==cycles for left in arms for right in arms if left!=right)
        assert all((r['flushed_lines']>0)==(r['condition']=='flushed-image') for r in rs)
    summary=read(q/'summary.json')
    csv_file(selected/'cells.csv',[{k:v for k,v in r.items() if k!='samples_ns'} for r in summary])
    csv_file(selected/'duplicates.csv',read(q/'duplicate-controls.json'))
    for file in ['setup.json','identity.json']:shutil.copy2(q/file,selected/file)
    for file in ['checks.json','report.txt']:shutil.copy2(full/file,selected/file)
    for file in ['lscpu.json','cache.txt','compiler.txt','input-artifact.json']:shutil.copy2(dest/file,selected/file)
    commands=[r for r in read(full/'compile_commands.json')
        if '/ikea-frame/encoding/' in r['file'] or '/ikea-frame/scan/' in r['file']]
    qualification=dict(job=job,source_digest=read(receipt)['source_digest'],
        all_source_files_match=len(wanted),library_sha256=identity['library_sha256'],
        checked_images=read(full/'checks.json')['images'],independent_checks=read(full/'checks.json')['checks'],
        samples=len(rows),executed_schedule_groups=len(groups),
        executed_position_and_predecessor_balance=True,all_raw_answers_agree=True,
        compile_commands=commands,scope='Single-core complete Python calls; no DRAM traffic or cache residency inference.')
    (selected/'qualification.json').write_text(json.dumps(qualification,indent=2)+'\n')
    print(name,len(rows),'samples;',len(groups),'executed schedules independently checked;',len(wanted),'source hashes match')
