"""Bounded, deterministic occurrence samples; outside any measured region."""
import json
import random
import struct
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import datasets

def get():
    inputs = {name: datasets.get(name) for name in ('real-roaring','msmarco-keyset')}
    identities = {name:datasets.verify(path)['id'] for name,path in inputs.items()}
    def build(out):
        counts = {}
        for family, directory in inputs.items():
            for path in sorted(directory.glob('*.kw16')):
                rows = [json.loads(line) for line in path.with_suffix('.windows.jsonl').read_text().splitlines()]
                rng = random.Random('bec-kernel-20260909/'+family+'/'+path.stem)
                selected = sorted(rng.sample(range(len(rows)),min(64,len(rows))))
                cells = []
                lineage = []
                with path.open('rb') as f:
                    for ordinal in selected:
                        r = rows[ordinal]
                        f.seek(r['offset'])
                        cardinality, = struct.unpack('<I',f.read(4))
                        assert cardinality == r['cardinality']
                        bitmap = bytearray(8192)
                        for (position,) in struct.iter_unpack('<H',f.read(cardinality*2)):
                            bitmap[position//8] |= 1 << (position%8)
                        for tile in sorted(rng.sample(range(256),16)):
                            cells.append(bytes(bitmap[tile*32:tile*32+32]))
                            lineage.append({'window':ordinal,'tile':tile})
                name = family+'--'+path.stem
                (out/(name+'.bits32')).write_bytes(b''.join(cells))
                (out/(name+'.json')).write_text(json.dumps(lineage)+'\n')
                counts[name] = len(cells)
        return {'sampling':'up to64 windows uniformly without replacement,16 of256 cells per window; empty/full included',
                'counts':counts,'source_ids':identities}
    return datasets.cached('ikea-bec-kernel-inputs',Path(__file__),identities,{},build)

if __name__=='__main__': print(get())
