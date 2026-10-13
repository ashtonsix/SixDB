#!/usr/bin/env python3
"""Append qualified Shared adapter endpoints to an existing selected bundle."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil

HERE=Path(__file__).resolve().parent
GEOMETRIES={(128,1),(128,32),(1048576,32),(4194304,8),(4194304,32)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index',type=Path,default=HERE/'shared-measurements.json')
    parser.add_argument('--workers',type=Path,default=Path('build/workers'))
    parser.add_argument('--output',type=Path,default=HERE/'selected')
    parser.add_argument('--codegen',type=Path,default=HERE/'shared-codegen.json')
    args=parser.parse_args()
    source=load(args.index);recovery=load(args.output/'recovery.json')
    assert source['jobs'] and all(j['worker_status']['state']=='complete' and j.get('measurement_rows',0)>0 for j in source['jobs'])
    assert args.codegen.exists(),'final Shared native codegen proof is required before retention'
    rows,jobs=[],[]
    for job in source['jobs']:
        job_id=job['job_id'];base=args.workers/job_id;root=base/'results'
        collection,original_job=load(base/'collection.json'),load(base/'job.json')
        members=collection['members']
        receipt={'job_id':job_id,'worker_uri':original_job['uri'],'machine':job['machine'],'instance_type':job['instance_type'],
                 'script':original_job['script'],'source':job['source'],'source_commit':job['source_commit'],
                 'result_archive':collection['artifact'],'source_archive_member':'source.tar.gz',
                 'worker_result':job['worker_status'],'local_job_sha256':sha(base/'job.json'),
                 'local_collection_sha256':sha(base/'collection.json'),'selected_members':{}}
        assert members['source.tar.gz']['sha256']==job['source']['sha256']
        def reference(name,expected=None):
            member=members[name];path=root/name
            if expected:assert member['sha256']==expected
            if path.exists():assert sha(path)==member['sha256']
            receipt['selected_members'][name]={'sha256':member['sha256'],'bytes':member['bytes']}
            return path
        item={k:v for k,v in job.items() if k not in ('cells','measurement_files')}
        item['measurement_files']=[]
        for m in job['measurement_files']:
            if (m['n'],m['chains']) not in GEOMETRIES:continue
            path=reference(m['measurement'],m['csv_sha256'])
            reference(m['measurement'].replace('.csv','.context'),m['context_sha256'])
            raw=list(csv.DictReader(path.open()));assert len(raw)==56
            rows.extend({'job_id':job_id,'isa':m['isa'],'transfer':m['transfer'],'window_mode':m['window_mode'],**row} for row in raw)
            item['measurement_files'].append(m)
        item['cells']=[{k:v for k,v in cell.items() if k!='samples'} for cell in job['cells'] if (cell['n'],cell['chains']) in GEOMETRIES]
        item['selected_rows']=sum(m['rows'] for m in item['measurement_files'])
        for name,digest in job['files'].items():
            path=reference(name,digest)
            if name.startswith(('loom-validation-','asan-qualification-')):
                destination=args.output/'shared-qualification'/job_id/name
                destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,destination)
                receipt['selected_members'][name]['retained_as']=str(destination.relative_to(args.output))
        for name,member in members.items():
            if name.startswith('assembly-') and ('adapter' in name or 'qualification' in name):
                reference(name)
        recovery['jobs'][job_id]=receipt
        jobs.append(item)
    expected=5*2*2*2*8*7*sum(len(j['isas']) for j in source['jobs'])
    assert len(rows)==expected
    fieldnames=list(dict.fromkeys(key for row in rows for key in row))
    with (args.output/'shared.csv').open('w') as stream:
        writer=csv.DictWriter(stream,fieldnames=fieldnames);writer.writeheader();writer.writerows(rows)
    retained={'format':1,'comparison_status':'measured negative performance cut: std::invoke semantic helpers outline in reusable Cohort',
              'behavior_qualification':'native and sanitizer checks passed; performance cut is not the final selected implementation',
              'expected_source_identity':source['expected_source_identity'],'source_audit':source['source_audit'],
              'independent_key_set_checksums':source['independent_key_set_checksums'],'jobs':jobs,
              'selected_raw_rows':len(rows),'raw_rows':'shared.csv','limits':source['limits']}
    (args.output/'shared.json').write_text(json.dumps(retained,indent=2)+'\n')
    shutil.copyfile(args.codegen,args.output/'shared-codegen.json')
    # Keep the corrected cause with its explicit cross-only scope. Full LLVM IR
    # and objects stay in ignored review storage; their hashes are in receipts.
    for name in ['x86-entry-asan-diagnosis.json','x86-entry-asan-independent.json']:
        shutil.copyfile(HERE/name,args.output/name)
    q=HERE/'shared-x86-fixed-qualification/receipt.json'
    destination=args.output/'shared-x86-fixed-qualification/receipt.json'
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(q,destination)
    caller=HERE.parent/'streampack-local/cursor-final-caller-qualification.json'
    if caller.exists():
        caller_receipt=load(caller)
        assert caller_receipt['status']=='pass' and caller_receipt['ring_sha256']==source['jobs'][0]['optimized_ring_sha256']
        shutil.copyfile(caller,args.output/'cursor-final-caller-qualification.json')
    scratch=HERE.parent/'static-restore-scratch/selected'
    if scratch.exists():
        # Peer owns the source bundle. Copy only its compact selected contents
        # and keep its original manifest identity; do not edit that packet.
        manifest=scratch/'manifest.json'
        if not manifest.exists():
            manifest=next(p for p in scratch.glob('*.json') if p.name.startswith('manifest'))
        scratch_manifest=load(manifest)
        assert len(scratch_manifest['files'])==scratch_manifest['file_count']==20
        for name,entry in scratch_manifest['files'].items():
            assert sha(scratch/name)==entry['sha256'] and (scratch/name).stat().st_size==entry['bytes']
        assert sum(entry['bytes'] for entry in scratch_manifest['files'].values())==scratch_manifest['selected_bytes']
        recovery['source_indices']['static-restore-scratch-selection']={'path':str(scratch),'manifest_sha256':sha(manifest)}
        shutil.copytree(scratch,args.output/'static-restore-scratch',dirs_exist_ok=True)
    for name in ['shared_audit.py','select_shared.py','inspect_shared_assembly.py','summarize_shared_codegen.py']:
        shutil.copyfile(HERE/name,args.output/'tools'/name)
    recovery['source_indices'][args.index.name]={'sha256':sha(args.index),'rebuild_script':'tools/shared_audit.py'}
    recovery['scope']['final_shared']={'geometries':sorted(GEOMETRIES),'transfers':['local','shared'],'window_modes':[0,1],
                                     'grains':[1,4],'repetitions':7,'arms':'all eight controls','native_rows':len(rows),
                                     'comparison_status':'measured negative reusable-Cohort performance cut; not promoted implementation'}
    recovery['counts']['shared_native_rows']=len(rows)
    recovery['counts']['shared_summary_cells']=sum(len(j['cells']) for j in jobs)
    recovery['files']=[{'path':str(path.relative_to(args.output)),'sha256':sha(path),'bytes':path.stat().st_size}
                       for path in sorted(args.output.rglob('*')) if path.is_file() and path.name!='recovery.json']
    (args.output/'recovery.json').write_text(json.dumps(recovery,indent=2)+'\n')
    print(args.output,'appended Shared rows',len(rows),'summary cells',recovery['counts']['shared_summary_cells'])


if __name__=='__main__':main()
