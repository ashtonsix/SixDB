"""Pinned Calico headers as test-only shared input, never a kernel dependency."""
import argparse
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import datasets

COMMIT = 'ac83c82b9a0c8d76bea92829e471ebc0d98b1978'
FILES = {
    'keyset/include/keyset/detail/enum_fast.h': '4db274b5986dedfcbbacb93e682c509c0bfaf821d1e5377fcaac0414bb2c2a08',
    'keyset/bench/p2_a5_enum.h': '355e9f9888791c6abeb6ca4082fb4688d6bed8822b987f155f79c62960f861b0',
}

def get(calico=None):
    reference = Path(__file__).with_name('prior-input.json')
    if reference.exists():
        return datasets.restore(json.loads(reference.read_text()))
    calico = Path(calico or ROOT.parent / 'calico').resolve()
    def build(out):
        for name, checksum in FILES.items():
            path = calico / name
            if datasets.digest(path) != checksum:
                raise ValueError(f'Prior source changed: {path}')
            shutil.copyfile(path, out / path.name)
        return {'repository': 'Calico', 'commit': COMMIT, 'files': FILES}
    return datasets.cached('ikea-bec-prior', Path(__file__), FILES, {'commit': COMMIT}, build)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--calico', type=Path)
    p.add_argument('--publish', action='store_true')
    a = p.parse_args()
    directory = get(a.calico)
    if a.publish:
        Path(__file__).with_name('prior-input.json').write_text(json.dumps(datasets.publish(directory), indent=2)+'\n')
    print(directory)
