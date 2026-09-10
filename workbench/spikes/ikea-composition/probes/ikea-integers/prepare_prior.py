"""Pinned current Calico comparand. Never a production kernel dependency."""
import argparse
import json
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'workbench/tools'))
import datasets

def get():
    reference=Path(__file__).with_name('prior-input.json')
    if reference.exists():
        return datasets.restore(json.loads(reference.read_text()))
    source=ROOT.parent/'calico/workbench/prototypes/bytepack'
    spec=json.loads(Path(__file__).with_name('prior-source.json').read_text())
    def build(out):
        for name,checksum in spec['files'].items():
            if datasets.digest(source/name)!=checksum:
                raise ValueError('Prior changed: '+name)
            (out/name).parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source/name,out/name)
        return spec
    return datasets.cached('ikea-integers-prior',Path(__file__),spec['files'],{'commit':spec['commit']},build)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publish',action='store_true')
    args=parser.parse_args()
    directory=get()
    if args.publish:
        Path(__file__).with_name('prior-input.json').write_text(json.dumps(datasets.publish(directory),indent=2)+'\n')
    print(directory)
