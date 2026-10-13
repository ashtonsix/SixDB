#!/usr/bin/env python3
"""Append the bounded final header-factor experiment without replacing negatives."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil

HERE=Path(__file__).resolve().parent
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path):return json.loads(path.read_text())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index',type=Path,default=HERE/'cohort-factor-measurements.json')
    parser.add_argument('--codegen',type=Path,default=HERE/'cohort-factor-codegen.json')
    parser.add_argument('--workers',type=Path,default=Path('build/workers'))
    parser.add_argument('--output',type=Path,default=HERE/'selected')
    args=parser.parse_args();source=load(args.index);recovery=load(args.output/'recovery.json')
    assert len(source['jobs'])==3 and all(j['worker_status']['state']=='complete' and j['measurement_rows']==896 for j in source['jobs'])
    codegen=load(args.codegen);assert codegen['qualification']=='actual old/new assembly, calls/frames/tails independently inspected'
    rows,jobs=[],[]
    for job in source['jobs']:
        job_id=job['job_id'];directory=args.workers/job_id;root=directory/'results'
        collection,original=load(directory/'collection.json'),load(directory/'job.json');members=collection['members']
        receipt={'job_id':job_id,'worker_uri':original['uri'],'machine':job['machine'],'instance_type':job['instance_type'],
            'script':original['script'],'source':job['source'],'source_commit':job['source_commit'],'result_archive':collection['artifact'],
            'source_archive_member':'source.tar.gz','worker_result':job['worker_status'],'local_job_sha256':sha(directory/'job.json'),
            'local_collection_sha256':sha(directory/'collection.json'),'selected_members':{}}
        assert members['source.tar.gz']['sha256']==job['source']['sha256']
        def reference(name,expected=None,retain=False):
            member=members[name];path=root/name
            if expected:assert member['sha256']==expected
            if path.exists():assert sha(path)==member['sha256']
            entry={'sha256':member['sha256'],'bytes':member['bytes']}
            if retain:
                destination=args.output/'cohort-factor-qualification'/job_id/name
                destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,destination)
                entry['retained_as']=str(destination.relative_to(args.output))
            receipt['selected_members'][name]=entry
            return path
        item={k:v for k,v in job.items() if k not in ('cells','reported_codegen')}
        item['cells']=[{k:v for k,v in cell.items() if k!='samples'} for cell in job['cells']]
        for measurement in job['measurement_files']:
            path=reference(measurement['measurement'],measurement['csv_sha256'])
            reference(measurement['measurement'].replace('.csv','.context'),measurement['context_sha256'])
            raw=list(csv.DictReader(path.open()));assert len(raw)==56
            rows.extend({'job_id':job_id,'isa':measurement['isa'],'call_factor':measurement['call_factor'],**row} for row in raw)
        for name,digest in job['files'].items():
            reference(name,digest,retain=name.startswith(('loom-validation-','asan-qualification-')))
        for name in members:
            if name.startswith('assembly-') and 'cohort_' in name:reference(name)
        recovery['jobs'][job_id]=receipt;jobs.append(item)
    assert len(rows)==2688
    keys=list(dict.fromkeys(key for row in rows for key in row))
    with (args.output/'cohort-factor.csv').open('w') as stream:
        writer=csv.DictWriter(stream,fieldnames=keys);writer.writeheader();writer.writerows(rows)
    retained={'format':1,'comparison_status':'bounded final header factor; invoke negative versus direct implementation',
        'source_identity':source['source_identity'],'source_audit':source['source_audit'],
        'independent_key_set_checksums':source['independent_key_set_checksums'],'jobs':jobs,
        'raw_rows':'cohort-factor.csv','selected_raw_rows':len(rows),'limits':source['limits']}
    (args.output/'cohort-factor.json').write_text(json.dumps(retained,indent=2)+'\n')
    shutil.copyfile(args.codegen,args.output/'cohort-factor-codegen.json')
    for name in ['cohort_factor_audit.py','select_cohort_factor.py','inspect_shared_assembly.py','summarize_shared_codegen.py','summarize_cohort_factor_codegen.py']:
        shutil.copyfile(HERE/name,args.output/'tools'/name)
    recovery['source_indices'][args.index.name]={'sha256':sha(args.index),'rebuild_script':'tools/cohort_factor_audit.py'}
    recovery['scope']['cohort_direct_factor']={'sizes':[128,4194304],'chains':[1,32],'grains':[1,4],'call_factor':['invoke','direct'],
        'window_mode':'cold_positive fixed','context_transfer':'Shared fixed','repetitions':7,'native_rows':len(rows)}
    recovery['counts']['cohort_direct_factor_rows']=len(rows)
    recovery['counts']['cohort_direct_factor_summary_cells']=sum(len(j['cells']) for j in jobs)
    recovery['files']=[{'path':str(p.relative_to(args.output)),'sha256':sha(p),'bytes':p.stat().st_size}
        for p in sorted(args.output.rglob('*')) if p.is_file() and p.name!='recovery.json']
    (args.output/'recovery.json').write_text(json.dumps(recovery,indent=2)+'\n')
    print(args.output,'appended2688 header-factor rows',recovery['counts'])


if __name__=='__main__':main()
