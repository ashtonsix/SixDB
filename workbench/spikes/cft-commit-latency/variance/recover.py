#!/usr/bin/env python3
"""Recover raw flow evidence and reproduce tables without cloud probes."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'workbench/tools'))
import artifacts


def main():
    p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    for name in ('workers.json','capture-config.json'):
        shutil.copyfile(args.evidence/name,args.output/name)
    refs=json.loads((args.output/'workers.json').read_text());paths=[]
    for name,entry in sorted(refs['members'].items()):
        assert entry['state']=='complete' and entry['artifact']
        reference=args.output/(name+'.artifact.json');reference.write_text(json.dumps(entry['artifact'],indent=2)+'\n')
        target=args.output/name;artifacts.fetch(reference,target);paths.append(str(target))
    subprocess.run([sys.executable,str(HERE.parent/'oneway/analyze.py'),*paths,'--output',str(args.output/'summary')],check=True)
    common=[sys.executable,str(HERE/'analyze.py'),str(args.output),'--inputs',str(args.output)]
    subprocess.run(common,check=True)
    config=json.loads((args.output/'capture-config.json').read_text())
    if int(next(iter(config.values())).get('ONEWAY_ROUNDS','4'))==5:
        subprocess.run(common+['--request-rank'],check=True)
    subprocess.run([sys.executable,str(HERE/'diagnose.py'),str(args.output),'--inputs',str(args.output)],check=True)


if __name__=='__main__':main()
