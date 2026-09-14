#!/usr/bin/env python3
"""Assemble selected summaries and immutable worker references, without copying raw runs."""
import argparse,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
GROUPS={
 'persistence':'storage/20260914T163736Z-fe9ebadd',
 'diagnostic':'d3-diagnostic/20260914T170103Z-90d24b33',
 'commit':'commit/20260914T165826Z-685010e6',
 'throughput':'throughput/20260914T175540Z-d8dc5d9e',
 'throughput-scale':'throughput-scale/20260914T182646Z-ae0e9d61',
 'throughput-express':'throughput-express/20260914T182936Z-db5f6b93'}
SUMMARIES={'persistence':'storage-summary','diagnostic':'d3-summary','commit':'commit-summary','throughput':'throughput-summary',
 'throughput-scale':'throughput-scale-summary','throughput-express':'throughput-express-summary'}
SELECTION={'persistence':['persistence.csv','persistence-passes.csv','devices.json'],
 'diagnostic':['d3-diagnostic.csv','d3-diagnostic-passes.csv','d3-request-traces.csv'],
 'commit':['commits.csv','commit-passes.csv','commit-nodes.json'],
 'throughput':['throughput.csv','throughput-passes.csv','throughput-nodes.json','knees.csv','network-counters.csv','network-context.json']}
for study in ['throughput-scale','throughput-express']:SELECTION[study]=SELECTION['throughput']


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--available',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 workers=[];included=[]
 for study,relative in GROUPS.items():
  group=json.loads((ROOT/'build/cft-commit-latency'/relative/'group.json').read_text())
  if a.available and not all((ROOT/'build/workers'/m['job']/'artifact.json').exists() for m in group['members'].values()):continue
  if a.available and not all((ROOT/'build/cft-commit-latency'/SUMMARIES[study]/file).exists() for file in SELECTION[study]):continue
  target=a.output/('throughput-small' if study=='throughput' else study) if study.startswith('throughput') else a.output
  target.mkdir(parents=True,exist_ok=True)
  (target/('campaign.json' if study.startswith('throughput') else study+'-campaign.json')).write_text(json.dumps(group,indent=2)+'\n');included.append(study)
  cohort_workers=[]
  for name,member in group['members'].items():
   folder=ROOT/'build/workers'/member['job'];job=json.loads((folder/'job.json').read_text());status=json.loads((folder/'status.json').read_text())
   assert status['state']=='complete' and status['script_returncode']==0,(study,name,status)
   cohort_workers.append(dict(study=study,name=name,job=job['id'],artifact=json.loads((folder/'artifact.json').read_text()),source_digest=job['source']['digest'],instance_type=job['config']['instance_type'],ami=job['config']['ami'],status=status))
  workers+=cohort_workers
  if study.startswith('throughput'):(target/'workers.json').write_text(json.dumps(cohort_workers,indent=2)+'\n')
  for file in SELECTION[study]:shutil.copyfile(ROOT/'build/cft-commit-latency'/SUMMARIES[study]/file,target/file)
  cleanup=ROOT/'build/cft-commit-latency'/SUMMARIES[study]/'resources.json'
  if cleanup.exists():shutil.copyfile(cleanup,target/'resources.json')
 (a.output/'workers.json').write_text(json.dumps(workers,indent=2)+'\n')
 print(json.dumps(dict(studies=included,workers=len(workers))))


if __name__=='__main__':main()
