#!/usr/bin/env python3
"""Adapt shared datasets to this probe's binary inputs; reuse unchanged variants."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import datasets


def write_strings(path, strings):
    with path.open('wb') as f:
        f.write(struct.pack('<I', len(strings)))
        for value in strings:
            b = value.encode('utf-8')
            f.write(struct.pack('<I', len(b)))
            f.write(b)


def prepare(output=None, blocks=64, block_rows=1024):
    uap = datasets.get('uap-core')
    accidents = datasets.get('accidents', blocks=blocks, block_rows=block_rows)
    parents = {'uap': datasets.verify(uap), 'accidents': datasets.verify(accidents)}

    def build(destination):
        fixtures = [json.loads(line) for line in (uap / 'fixtures.jsonl').read_text().splitlines()]
        write_strings(destination / 'uap.strings', [r['record']['user_agent_string'] for r in fixtures])
        origins = [[r['source'], r['row']] for r in fixtures]
        (destination / 'uap-origins.json').write_text(json.dumps(origins, separators=(',', ':')) + '\n')
        shutil.copyfile(uap / 'LICENSE', destination / 'uap-LICENSE')
        with (destination / 'uap.patterns').open('w', newline='\n') as f:
            for line in (uap / 'rules.jsonl').read_text().splitlines():
                row = json.loads(line)
                flag = row['record'].get('regex_flag', '')
                if flag not in ('', 'i'):
                    raise ValueError(f'Unrecognized flag: {flag}')
                pattern = row['record']['regex'].encode().hex()
                f.write(f'{row["id"]}\t{row["group"]}\t{int(flag == "i")}\t{pattern}\n')
        descriptions = [json.loads(line)['description'] for line in (accidents / 'descriptions.jsonl').read_text().splitlines()]
        write_strings(destination / 'accidents.strings', descriptions)
        queries = [s for s in (accidents / 'queries.txt').read_text().splitlines() if s.strip()]
        with (destination / 'accidents.patterns').open('w', newline='\n') as f:
            for i, pattern in enumerate(queries):
                f.write(f'accident:{i}\taccidents\t0\t{pattern.encode().hex()}\n')
        sources = {}
        for name in ('uap-core', 'accidents'):
            spec = json.loads((ROOT / 'workbench/datasets' / name / 'dataset.json').read_text())
            sources.update({k: {f: v[f] for f in ('url', 'sha256', 'bytes')} for k, v in spec['sources'].items()})
        meta = {'sources': sources, 'uap': parents['uap']['details'], 'accidents': parents['accidents']['details'],
                'semantics': 'RE2 UTF-8 case-sensitive search, except explicit uap regex_flag=i; Boolean results only',
                'prepared_sha256': datasets.file_hashes(destination)}
        (destination / 'inputs.json').write_text(json.dumps(meta, indent=2, sort_keys=True) + '\n')
        return {'workload': 'independent Boolean queries; pairing/order belong to the probe',
                'parents': {k: v['id'] for k, v in parents.items()}}

    prepared = datasets.cached('regexp-lowering-inputs', Path(__file__),
                               {k: v['id'] for k, v in parents.items()},
                               {'blocks': blocks, 'block_rows': block_rows}, build)
    if output is not None:
        output = Path(output).resolve()
        if output != prepared:
            output.mkdir(parents=True, exist_ok=True)
            for path in prepared.iterdir():
                dest = output / path.name
                if dest.exists() and dest.read_bytes() != path.read_bytes():
                    raise ValueError(f'Output already contains different inputs: {dest}')
                if not dest.exists():
                    shutil.copyfile(path, dest)
        return output
    return prepared


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', nargs='?', type=Path, help='Optional compatibility copy; normally use the returned cache path')
    p.add_argument('--blocks', type=int, default=64)
    p.add_argument('--block-rows', type=int, default=1024)
    a = p.parse_args()
    print(prepare(a.output, a.blocks, a.block_rows))
