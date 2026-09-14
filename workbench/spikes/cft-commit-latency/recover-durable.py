#!/usr/bin/env python3
"""Verify/fetch immutable durable-study archives and regenerate all numeric evidence."""
import argparse,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
import artifacts


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--study',choices=['persistence','diagnostic','commit','throughput','throughput-scale','throughput-express']);a=p.parse_args()
 if a.output.exists():raise FileExistsError('choose a new recovery directory')
 a.output.mkdir(parents=True);groups={};entries={}
 for manifest in sorted(a.evidence.rglob('workers.json')):
  for entry in json.loads(manifest.read_text()):entries[entry['job']]=entry
 for entry in entries.values():
  if a.study and entry['study']!=a.study:continue
  ref=a.output/(entry['job']+'.artifact.json');ref.write_text(json.dumps(entry['artifact'],indent=2)+'\n')
  folder=a.output/entry['job'];artifacts.fetch(ref,folder)
  groups.setdefault(entry['study'],[]).append((entry['name'],folder))
 if a.study and a.study not in groups:raise ValueError('no archived workers for requested study')
 scripts={'persistence':'persistence/analyze.py','diagnostic':'persistence/diagnostic_analyze.py','commit':'commit/analyze.py','throughput':'commit/throughput_analyze.py'}
 for study in ['throughput-scale','throughput-express']:scripts[study]=scripts['throughput']
 for study,inputs in groups.items():
  source=[str(inputs[0][1])] if study=='diagnostic' else [f'{name}={folder}' for name,folder in inputs]
  subprocess.run([sys.executable,str(Path(__file__).parent/scripts[study]),*source,'--output',str(a.output/study)],check=True)


if __name__=='__main__':main()
