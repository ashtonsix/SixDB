#!/usr/bin/env python3
"""Small original-order IEEE float columns; no decimal scaling or NULL cleanup."""
import argparse
from contextlib import ExitStack
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import datasets

# Same verified source/cache as phase-1 encoding/jena_parts.py. These outputs
# preserve IEEE values instead of converting decimal text to exact scaled ints.
JENA = dict(url='https://storage.googleapis.com/tensorflow/tf-keras-datasets/jena_climate_2009_2016.csv.zip',
    bytes=13568290, sha256='63d757501e92284a7de7cdbef0337f03b24e13ead7ac2b5b8c86a18d8e38ba5b')
ALP_REV = '31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce'
ALP = dict(url=f'https://codeload.github.com/cwida/ALP/tar.gz/{ALP_REV}', bytes=9365276,
    sha256='080807a58508ad4041f00251c14feae3a984d7630046f55aa42b375fb491b6f3',
    legacy_cache=f'build/peers/frame/alp-{ALP_REV}.tar.gz')


class Column:
    def __init__(self, output, name, field, source, **coordinates):
        self.path = output / (name + '.f64')
        self.file = self.path.open('wb')
        self.lexemes = hashlib.sha256()
        self.details = dict(file=self.path.name, name=name, source_field=field,
            source=source, dtype='Float64', byte_order='little', rows=0,
            nan=0, infinity=0, negative_zero=0, **coordinates)
        self.first = []; self.last = None

    def append(self, text):
        value = float(text)
        encoded = struct.pack('<d', value)
        self.file.write(encoded)
        self.lexemes.update(text.encode('utf-8') + b'\n')
        self.details['rows'] += 1
        self.details['nan'] += int(math.isnan(value))
        self.details['infinity'] += int(math.isinf(value))
        self.details['negative_zero'] += int(value == 0 and math.copysign(1, value) < 0)
        item = dict(text=text, bits_le=encoded.hex())
        if len(self.first) < 3: self.first.append(item)
        self.last = item

    def finish(self, rows):
        self.file.close()
        assert self.details['rows'] == rows and self.path.stat().st_size == rows * 8
        with self.path.open('rb') as stream:
            for item in self.first:
                assert stream.read(8).hex() == item['bits_le']
            stream.seek(-8, 2)
            assert stream.read(8).hex() == self.last['bits_le']
        return self.details | dict(selected_lexemes_sha256=self.lexemes.hexdigest(),
            first=self.first, last=self.last)


def prepare(rows):
    if not 0 < rows <= 196608:
        raise ValueError('This local slice recipe supports 1..196608 rows; larger corpora belong on workers')
    assert sys.float_info.radix == 2 and sys.float_info.mant_dig == 53
    jena = datasets.source(JENA)
    alp = datasets.source(ALP)

    def build(output):
        columns = []
        with zipfile.ZipFile(jena) as archive:
            name = 'jena_climate_2009_2016.csv'
            sha = hashlib.sha256()
            with archive.open(name) as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''): sha.update(chunk)
            assert sha.hexdigest() == '9626d3964f7afb3fa94b6f3d3a0e92fe96e4a4477c31ed9875cc64d6717c129f'
            with archive.open(name) as stream, ExitStack() as stack:
                reader = csv.reader(io.TextIOWrapper(stream, encoding='utf-8', newline=''))
                header = next(reader)
                assert header[0] == 'Date Time' and len(header) == 15
                outputs = [Column(output, f'jena-{i:02d}', field, 'Jena climate',
                    source_row_start=0, order='original CSV row order') for i, field in enumerate(header[1:])]
                for column in outputs: stack.callback(column.file.close)
                first_date = last_date = None
                for index, record in enumerate(reader):
                    assert len(record) == len(header)
                    if first_date is None: first_date = record[0]
                    last_date = record[0]
                    for column, text in zip(outputs, record[1:]): column.append(text)
                    if index + 1 == rows: break
                columns += [column.finish(rows) | dict(first_date_text=first_date, last_date_text=last_date)
                            for column in outputs]
        with tarfile.open(alp) as archive:
            for source, suffix, label in [
                ('ALP active_power fixture', '/data/evalimplsts/active_power.csv', 'active-power'),
                ('ALP ShapesAll fixture', '/data/issue/ShapesAll_TEST.csv', 'shapes-all')]:
                members = [m for m in archive.getmembers() if m.name.endswith(suffix)]
                assert len(members) == 1 and members[0].isfile()
                column = Column(output, label, suffix, source, source_row_start=0,
                    order='reference fixture column order; no inferred series boundaries')
                try:
                    with archive.extractfile(members[0]) as stream:
                        for index, raw in enumerate(stream):
                            text = raw.decode('ascii').strip()
                            assert text and ',' not in text
                            column.append(text)
                            if index + 1 == rows: break
                    columns.append(column.finish(rows))
                finally: column.file.close()
        manifest = dict(columns=columns, sources=[JENA, ALP], alp_revision=ALP_REV,
            mapping='Original numeric lexemes parsed to IEEE binary64; no quantization or offset mapping.',
            null_policy='No injected NULLs or cleanup. Jena -9999 sentinels remain numeric values.',
            selection='Bounded deterministic prefixes, not representative population samples.',
            scope='Reusable float/time-series inputs; no encoding or performance result.')
        (output / 'columns.json').write_text(json.dumps(manifest, indent=2) + '\n')
        return manifest

    return datasets.cached('frame-ieee-columns', Path(__file__), [JENA, ALP], {'rows': rows}, build)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', type=int, default=65536)
    args = parser.parse_args()
    print(prepare(args.rows))
