#!/usr/bin/env python3
"""Retrieve pinned public inputs; prepare raw strings without synthesizing queries."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import tarfile
import urllib.request
import yaml

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
UAP = '73e7340c3ed8055051607b296bf46ead7aa5f19e'
BLARE = 'b3c2a344307aed76e70ad1c40fd772c160d89425'
ACCIDENT_SHA = '6029cb1eb91a607ba9e08f58ad46297129c7e11b1fd83dfbc20a585db2fca885'

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def fetch(url, path, expected=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        temporary = path.with_suffix('.partial')
        with urllib.request.urlopen(url, timeout=120) as src, temporary.open('wb') as out:
            while chunk := src.read(1024 * 1024):
                out.write(chunk)
        temporary.replace(path)
    digest = sha(path)
    if expected and digest != expected:
        raise ValueError(f'Hash mismatch: {path}')
    return {'url': url, 'sha256': digest, 'bytes': path.stat().st_size}

def write_strings(path, strings):
    with path.open('wb') as f:
        f.write(struct.pack('<I', len(strings)))
        for value in strings:
            b = value.encode('utf-8')
            f.write(struct.pack('<I', len(b))); f.write(b)

def prepare(output, blocks=64, block_rows=1024):
    output.mkdir(parents=True, exist_ok=True)
    cache = ROOT / 'build/datasets/regexp-lowering'
    sources = {}
    archive = cache / f'uap-{UAP}.tar.gz'
    sources['uap'] = fetch(f'https://codeload.github.com/ua-parser/uap-core/tar.gz/{UAP}', archive)
    with tarfile.open(archive) as tar:
        prefix = f'uap-core-{UAP}/'
        def read(name):
            return tar.extractfile(prefix + name).read()
        rules = yaml.safe_load(read('regexes.yaml'))
        strings, origins = [], []
        for name in ('test_ua.yaml', 'test_os.yaml', 'test_device.yaml'):
            for i, row in enumerate(yaml.safe_load(read('tests/' + name))['test_cases']):
                strings.append(row['user_agent_string']); origins.append([name, i])
        (output/'uap-LICENSE').write_bytes(read('LICENSE'))
    write_strings(output/'uap.strings', strings)
    with (output/'uap.patterns').open('w') as f:
        for group, entries in rules.items():
            for i, row in enumerate(entries):
                flag = row.get('regex_flag', '')
                if flag not in ('', 'i'): raise ValueError(f'Unrecognized flag: {flag}')
                f.write(f'{group}:{i}\t{group}\t{int(flag == "i")}\t{row["regex"].encode().hex()}\n')
    uap_info = {'strings': len(strings), 'unique_strings': len(set(strings)),
                'pattern_groups': {k: len(v) for k, v in rules.items()},
                'input_order': 'test_ua, test_os, test_device; source order, duplicates retained'}
    (output/'uap-origins.json').write_text(json.dumps(origins, separators=(',', ':'))+'\n')
    accident = cache/'accidents.csv'
    sources['accidents'] = fetch(f'https://media.githubusercontent.com/media/mush-zhang/Blare/{BLARE}/data/US_Accidents_Dec21_updated.csv', accident, ACCIDENT_SHA)
    queryfile = cache/f'accidents-{BLARE}.regex'
    sources['accident_queries'] = fetch(f'https://raw.githubusercontent.com/mush-zhang/Blare/{BLARE}/data/regexes_traffic.txt', queryfile)
    with accident.open(newline='') as f:
        reader = csv.DictReader(f)
        total = sum(1 for _ in reader)
    if blocks*block_rows > total: raise ValueError('Sample exceeds available rows')
    starts = [i*(total-block_rows)//max(1, blocks-1) for i in range(blocks)]
    selected, block = [], 0
    with accident.open(newline='') as f:
        for i, row in enumerate(csv.DictReader(f)):
            while block < blocks and i >= starts[block]+block_rows: block += 1
            if block < blocks and starts[block] <= i: selected.append(row['Description'])
    assert len(selected) == blocks*block_rows
    write_strings(output/'accidents.strings', selected)
    queries = [x for x in queryfile.read_text().splitlines() if x.strip()]
    with (output/'accidents.patterns').open('w') as f:
        for i, p in enumerate(queries): f.write(f'accident:{i}\taccidents\t0\t{p.encode().hex()}\n')
    accident_info = {'source_rows': total, 'strings': len(selected), 'unique_strings': len(set(selected)),
                     'patterns': len(queries), 'block_rows': block_rows, 'block_starts': starts,
                     'selection': 'evenly spaced contiguous source blocks, source order within blocks'}
    info = {'sources': sources, 'uap': uap_info, 'accidents': accident_info,
            'semantics': 'RE2 UTF-8 case-sensitive search, except explicit uap regex_flag=i; Boolean results only',
            'prepared_sha256': {p.name: sha(p) for p in sorted(output.glob('*')) if p.is_file() and p.name != 'inputs.json'}}
    (output/'inputs.json').write_text(json.dumps(info, indent=2, sort_keys=True)+'\n')
    print(json.dumps({'uap': uap_info, 'accidents': accident_info}, indent=2))

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    p.add_argument('--blocks', type=int, default=64)
    p.add_argument('--block-rows', type=int, default=1024)
    a = p.parse_args()
    if a.blocks < 1 or a.block_rows < 1: p.error('Positive sample geometry required')
    prepare(a.output, a.blocks, a.block_rows)
