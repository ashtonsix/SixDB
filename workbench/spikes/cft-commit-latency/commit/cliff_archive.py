#!/usr/bin/env python3
"""Assemble compact cliff evidence, retaining references to immutable raw worker archives."""
import argparse,json,shutil,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
GROUPS={'small':'small/20260914T214202Z-3ab6a170','express':'express/20260914T214904Z-2c3df909','local':'local-rerun','failed-local':'local'}
SCRIPTS=['cliff_analyze.py','cliff_original.py','cliff_plot.py','cliff_recover.py','cliff_archive.py']

def main(root,output):
    output.mkdir(parents=True,exist_ok=True);workers=[];campaigns={};verification=[]
    for cohort,relative in GROUPS.items():
        group=json.loads((root/relative/'group.json').read_text());campaigns[cohort]=group
        for member,value in group['members'].items():
            folder=ROOT/'build/workers'/value['job'];job=json.loads((folder/'job.json').read_text());status=json.loads((folder/'status.json').read_text())
            entry={'cohort':cohort,'member':member,'job':value['job'],'artifact':json.loads((folder/'artifact.json').read_text()),'source_digest':job['source']['digest'],'status':status,
                   'hardware':job['config']['hardware'],'instance_type':job['config']['instance_type'],'ami':job['config']['ami']}
            workers.append(entry)
            cases=folder/'results/cliff-cases.json'
            if cohort!='failed-local':
                assert status['state']=='complete' and status['script_returncode']==0
                for row in json.loads(cases.read_text()):
                    assert row['verified_records']==row['count']
                    verification.append({'cohort':cohort,'member':member,'key':row['key'],'records':row['count'],'verified_records':row['verified_records']})
    # The old trace is shared input; its original archive remains the source of truth.
    old=ROOT/'build/workers/20260914T175555Z-78dc2893'
    workers.append({'cohort':'original','member':'use1-az4','job':old.name,'artifact':json.loads((old/'artifact.json').read_text())})
    for cohort in ['small','local','express','original']:
        source=root/('analysis-'+cohort);target=output/cohort;target.mkdir(exist_ok=True)
        for name in ['cases.csv','seconds.csv','resource-seconds.csv','nvme.csv','preparation.csv','analysis.json','original-cases.csv','original-seconds.csv','original-analysis.json']:
            if (source/name).exists():shutil.copyfile(source/name,target/name)
    shutil.copytree(root/'images',output/'images',dirs_exist_ok=True)
    shutil.copyfile(root/'resources.json',output/'resources.json')
    (output/'workers.json').write_text(json.dumps(workers,indent=2)+'\n')
    (output/'campaigns.json').write_text(json.dumps(campaigns,indent=2)+'\n')
    (output/'verification.json').write_text(json.dumps(verification,indent=2)+'\n')
    (output/'selection.json').write_text(json.dumps({'purpose':'Causal cliff probe, preparation/controller limits and same-demand latency/throughput decisions','raw':'all per-record stages, monitors, preparation ledgers and exact captured sources recover through workers.json','failed_local':'native count guard rejected the first local-only attempt before measurement because environment flags were not forwarded; replaced by fresh successful cohort','analysis_sources':SCRIPTS,'selection':'all tested successful policies, including unfavorable comparisons; original high-rate repeats reanalyzed as shared input'},indent=2)+'\n')
    with tarfile.open(output/'analysis-source.tar.gz','w:gz') as tar:
        for name in SCRIPTS:tar.add(Path(__file__).parent/name,arcname=name)
    print(json.dumps({'workers':len(workers),'new_successful_voter_runs':len(verification),'verified_records_across_voters':sum(r['records'] for r in verification)}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();main(a.root,a.output)
