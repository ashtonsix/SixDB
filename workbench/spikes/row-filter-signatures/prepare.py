"""Reuse cached source descriptions; add only a length-prefixed byte adapter."""
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'workbench/tools'))
import datasets


def prepare():
    source = datasets.get('accidents')
    identity = datasets.verify(source)['id']

    def build(output):
        count = 0
        with (source / 'descriptions.jsonl').open() as inp, (output / 'strings.bin').open('wb') as out:
            for line in inp:
                value = json.loads(line)['description'].encode('utf-8')
                out.write(struct.pack('<I', len(value)))
                out.write(value)
                count += 1
        return {'strings': count, 'semantics': 'unchanged UTF-8 bytes, source order, duplicates preserved'}

    return datasets.cached('row-signature-text', Path(__file__), [identity], {}, build)


if __name__ == '__main__':
    print(prepare())
