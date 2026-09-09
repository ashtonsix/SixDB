#!/usr/bin/env python3
"""Check source lineage, window reconstruction, and historical compatibility offline."""
from array import array
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def recipe(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'workbench/datasets' / name / 'prepare.py')
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot load dataset recipe: {name}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RR = recipe('real-roaring')
MS = recipe('msmarco-keyset')


class KeysetInputs(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.output = self.root / 'out'
        self.output.mkdir()

    def archive(self, entries, name='census_srt'):
        path = self.root / (name + '.zip')
        with zipfile.ZipFile(path, 'w') as z:
            for member, data in entries.items():
                z.writestr(member, data)
        return {path.name: path}

    def read_windows(self, name):
        rows = [json.loads(x) for x in (self.output / (name + '.windows.jsonl')).read_text().splitlines()]
        blob = (self.output / (name + '.kw16')).read_bytes()
        result = []
        end = 0
        for row in rows:
            self.assertEqual(row['offset'], end)
            n, = struct.unpack_from('<I', blob, end)
            self.assertEqual(n, row['cardinality'])
            positions = struct.unpack_from(f'<{n}H', blob, end + 4)
            result.extend((row['high16'] << 16) | p for p in positions)
            end += 4 + 2 * n
        self.assertEqual(end, len(blob))
        return rows, result

    def test_roaring_lineage_duplicates_and_sparse_windows(self):
        raw = b'0,255,256\n65535,65536,4294967295\n'
        sources = self.archive({'z.txt': raw, 'a.txt': raw, 'empty.txt': b'', 'skip.md': b'ignore'})
        info = RR.prepare(sources, self.output, 1)
        rows, values = self.read_windows('census_srt')
        self.assertEqual(values, [0, 255, 256, 65535, 65536, 4294967295])
        self.assertEqual([x['high16'] for x in rows], [0, 1, 65535])
        self.assertEqual({x['bitmap_id'] for x in rows}, {hashlib.sha256(raw).hexdigest()})
        origins = [json.loads(x) for x in (self.output / 'bitmaps.jsonl').read_text().splitlines()]
        self.assertEqual(origins[-1]['duplicate_of'], 'a.txt')
        self.assertEqual(info['datasets']['census_srt']['family'], 'census')
        self.assertEqual(info['datasets']['census_srt']['duplicate_files'], 1)

    def test_historical_floor_and_hash_check(self):
        sources = self.archive({'a.txt': ','.join(map(str, list(range(16)) + [65536]))})
        expected = struct.pack('<I16H', 16, *range(16))
        manifest = self.root / 'MANIFEST.sha256'
        manifest.write_text(hashlib.sha256(expected).hexdigest() + '  census_srt.kw16\n')
        sources[manifest.name] = manifest
        result = RR.prepare(sources, self.output, 16)
        self.assertEqual((self.output / 'census_srt.kw16').read_bytes(), expected)
        self.assertEqual(result['historical_kw16_verified'], ['census_srt.kw16'])
        self.assertEqual(result['datasets']['census_srt']['omitted_windows'], 1)
        manifest.write_text('0' * 64 + '  census_srt.kw16\n')
        with self.assertRaisesRegex(ValueError, 'Historical'):
            RR.prepare(sources, self.output, 16)

    def test_text_stream_boundaries(self):
        class Chunks(io.BytesIO):
            def read(self, size=-1):
                return super().read(min(size, 3))
        self.assertEqual(list(RR.integers(Chunks(b'123456, \n789\r\n4294967295'))),
                         [123456, 789, 4294967295])
        with self.assertRaises(ValueError):
            list(RR.integers(io.BytesIO(b'1,-1')))

    def test_reject_invalid_roaring_order_and_range(self):
        for raw in ('2,1', '1,1', '4294967296'):
            with self.subTest(raw=raw), self.assertRaisesRegex(ValueError, 'strictly increasing u32'):
                RR.prepare(self.archive({'a.txt': raw}), self.output, 1)

    def test_msmarco_reconstruct_across_chunks_and_empty_term(self):
        values = list(range(17000)) + [65536, 65538, 4294967295]
        path = self.root / 'term.u32'
        path.write_bytes(struct.pack(f'<{len(values)}I', *values))
        empty = self.root / 'empty.u32'
        empty.write_bytes(b'')
        result = MS.prepare({'term.u32': path, 'empty.u32': empty}, self.output, 1)
        rows, decoded = self.read_windows('term')
        self.assertEqual(decoded, values)
        self.assertEqual({r['term'] for r in rows}, {'term'})
        self.assertEqual(result['terms']['term']['source_positions'], len(values))
        self.assertEqual(self.read_windows('empty'), ([], []))
        MS.prepare({'term.u32': path}, self.output, 16)
        self.assertEqual(self.read_windows('term')[1], list(range(17000)))

    def test_msmarco_reject_corrupt_and_unordered_input(self):
        path = self.root / 'term.u32'
        for raw in (b'123', struct.pack('<II', 1, 1), struct.pack('<II', 2, 1)):
            path.write_bytes(raw)
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                MS.prepare({'term.u32': path}, self.output, 1)

    def test_invalid_parameters(self):
        for module in (RR, MS):
            for value in (0, 65537, True, 1.5, '16'):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    module.prepare({}, self.output, value)


def full_check():
    """Optional real-data check: reconstruct original IDs using the sidecars."""
    import datasets

    def u32_bytes(values):
        result = array('I', values)
        if sys.byteorder != 'little':
            result.byteswap()
        return result.tobytes()

    def reconstruct(directory, name, group_key):
        hashes = {}
        with (directory / (name + '.kw16')).open('rb') as data:
            for ordinal, line in enumerate((directory / (name + '.windows.jsonl')).open()):
                row = json.loads(line)
                assert row['ordinal'] == ordinal and row['offset'] == data.tell()
                count, = struct.unpack('<I', data.read(4))
                assert 1 <= count <= 65536 and count == row['cardinality']
                values = struct.unpack(f'<{count}H', data.read(count * 2))
                hashes.setdefault(row[group_key], hashlib.sha256()).update(
                    u32_bytes((row['high16'] << 16) | v for v in values))
            assert not data.read(1), 'Unindexed trailing windows'
        return {k: v.hexdigest() for k, v in hashes.items()}

    reports = {}
    for name in ('real-roaring', 'msmarco-keyset'):
        directory = datasets.get(name)
        meta = datasets.verify(directory)
        spec = json.loads((ROOT / 'workbench/datasets' / name / 'dataset.json').read_text())
        lists = 0
        for filename, source_spec in spec['sources'].items():
            if filename.endswith('.u32'):
                term = filename[:-4]
                actual = reconstruct(directory, term, 'term')
                assert actual[term] == source_spec['sha256'], term
                lists += 1
            elif filename.endswith('.zip'):
                actual = reconstruct(directory, filename[:-4], 'bitmap_id')
                seen = set()
                with zipfile.ZipFile(datasets.source(source_spec)) as archive:
                    for member in archive.namelist():
                        if not member.endswith('.txt'):
                            continue
                        raw = archive.read(member)
                        key = hashlib.sha256(raw).hexdigest()
                        if key in seen:
                            continue
                        seen.add(key)
                        # Independent whole-member parser for the optional audit;
                        # production preparation streams bounded chunks instead.
                        values = (int(x) for x in raw.replace(b'\n', b',').split(b',') if x.strip())
                        expected = hashlib.sha256(u32_bytes(values)).hexdigest()
                        assert actual.get(key, hashlib.sha256(b'').hexdigest()) == expected, member
                        lists += 1
                assert actual.keys() <= seen
        floor = datasets.verify(datasets.get(name, min_cardinality=16))
        groups = meta['details'].get('datasets', meta['details'].get('terms'))
        reports[name] = {'default_id': meta['id'], 'floor16_id': floor['id'],
                         'reconstructed_source_lists': lists,
                         'windows': sum(g['windows'] for g in groups.values()),
                         'positions': sum(g['positions'] for g in groups.values()),
                         'historical_kw16_verified': floor['details'].get('historical_kw16_verified', [])}
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--full', action='store_true', help='also resolve and reconstruct the pinned corpora (S3 on a cold cache)')
    args = parser.parse_args()
    tests = unittest.main(argv=[sys.argv[0]], exit=False)
    if not tests.result.wasSuccessful():
        sys.exit(1)
    if args.full:
        full_check()
