#!/usr/bin/env python3
"""Fetch immutable cliff inputs and reconstruct retained numeric evidence."""
import argparse,concurrent.futures,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
import artifacts

def main(evidence,output,cohort,analysis_sources):
    if output.exists():raise FileExistsError('choose a new recovery directory')
    output.mkdir(parents=True)
    workers=json.loads((evidence/'workers.json').read_text())
    selected=[w for w in workers if w['cohort']!='failed-local' and (not cohort or w['cohort']==cohort)]
    def fetch(w):
        target=output/('raw-'+w['cohort'])/w['member'];target.parent.mkdir(exist_ok=True)
        reference=output/(w['job']+'.artifact.json');reference.write_text(json.dumps(w['artifact'],indent=2)+'\n');artifacts.fetch(reference,target)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch,selected))
    for name in sorted({w['cohort'] for w in selected}):
        if name=='original':script='cliff_original.py';leader=output/'raw-original/use1-az4'
        else:script='cliff_analyze.py';leader=output/('raw-'+name)/('local-nvme' if name=='local' else 'use1-az4')
        subprocess.run([sys.executable,str(analysis_sources/script),str(leader),'--output',str(output/('analysis-'+name))],check=True)
        source=evidence/name;reconstructed=output/('analysis-'+name)
        for path in sorted(source.glob('*.csv')):
            assert path.read_bytes()==(reconstructed/path.name).read_bytes(),f'different reconstruction: {name}/{path.name}'
    (output/'recovery-verified.json').write_text(json.dumps({'cohorts':sorted({w['cohort'] for w in selected}),'workers':len(selected),'comparison':'all selected numeric CSV bytes identical'},indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--cohort',choices=['small','local','express','original']);p.add_argument('--analysis-sources',type=Path,default=Path(__file__).parent);a=p.parse_args();main(a.evidence,a.output,a.cohort,a.analysis_sources)
