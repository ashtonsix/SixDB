"""Whole-window occurrence samples; all256 cells including empty/full retained."""
import json
from pathlib import Path
import random
import struct
import sys
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import datasets


def get():
    source = datasets.get('real-roaring')
    identity = datasets.verify(source)['id']

    def build(out):
        lineage = {}
        for path in sorted(source.glob('*.kw16')):
            rows = [json.loads(line) for line in path.with_suffix('.windows.jsonl').read_text().splitlines()]
            rng = random.Random('ikea-heterogeneous-20260910/' + path.stem)
            selected = sorted(rng.sample(range(len(rows)), min(8, len(rows))))
            windows = []
            with path.open('rb') as stream:
                for ordinal in selected:
                    row = rows[ordinal]
                    stream.seek(row['offset'])
                    population, = struct.unpack('<I', stream.read(4))
                    assert population == row['cardinality']
                    data = bytearray(8192)
                    for (position,) in struct.iter_unpack('<H', stream.read(population * 2)):
                        data[position // 8] |= 1 << (position % 8)
                    windows.append(data)
            (out / (path.stem + '.windows')).write_bytes(b''.join(windows))
            lineage[path.stem] = [rows[i] for i in selected]
        (out / 'lineage.json').write_text(json.dumps(lineage, indent=2, sort_keys=True) + '\n')
        return {'source_id': identity, 'sampling': 'up to8 whole windows per archive without replacement; all256 cells retained', 'windows': {k: len(v) for k, v in lineage.items()}}
    return datasets.cached('ikea-heterogeneous-windows', Path(__file__), {'real-roaring': identity}, {}, build)


if __name__ == '__main__':
    print(get())
