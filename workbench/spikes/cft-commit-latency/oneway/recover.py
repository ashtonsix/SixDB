#!/usr/bin/env python3
"""Recover the immutable worker archives and recompute all directional tables."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
import artifacts


def main():
    p=argparse.ArgumentParser()
    p.add_argument('evidence',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    workers=json.loads((args.evidence/'workers.json').read_text())
    sources=[]
    for name,entry in sorted(workers['members'].items()):
        assert entry['state']=='complete' and entry['artifact']
        ref=args.output/(name+'.artifact.json')
        ref.write_text(json.dumps(entry['artifact'],indent=2)+'\n')
        target=args.output/name
        artifacts.fetch(ref,target)
        sources.append(str(target))
    subprocess.run([sys.executable,str(Path(__file__).with_name('analyze.py')),*sources,
                    '--output',str(args.output/'summary')],check=True)


if __name__=='__main__':
    main()
