"""Sample original descriptions in source blocks; retain the published queries."""
import csv
import json
import shutil

DEFAULTS = {'blocks': 64, 'block_rows': 1024}
# Parsed count of the SHA-256-pinned December 2021 file, checked on every build.
SOURCE_ROWS = 2845342


def prepare(sources, output, blocks, block_rows):
    if not all(type(n) is int and n > 0 for n in (blocks, block_rows)):
        raise ValueError('Positive integer sample geometry required')
    if blocks * block_rows > SOURCE_ROWS:
        raise ValueError('Sample exceeds available rows')
    starts = [i * (SOURCE_ROWS - block_rows) // max(1, blocks - 1) for i in range(blocks)]
    values, block, total = [], 0, 0
    with sources['accidents'].open(newline='', encoding='utf-8') as src, \
            (output / 'descriptions.jsonl').open('w', encoding='utf-8', newline='\n') as dst:
        for i, row in enumerate(csv.DictReader(src)):
            total += 1
            while block < blocks and i >= starts[block] + block_rows:
                block += 1
            if block < blocks and starts[block] <= i:
                values.append(row['Description'])
                dst.write(json.dumps({'row': i, 'description': row['Description']}) + '\n')
    if total != SOURCE_ROWS or len(values) != blocks * block_rows:
        raise ValueError('Source count or sample selection changed')
    shutil.copyfile(sources['accident_queries'], output / 'queries.txt')
    queries = [x for x in (output / 'queries.txt').read_text().splitlines() if x.strip()]
    return {'source_rows': total, 'strings': len(values), 'unique_strings': len(set(values)),
            'patterns': len(queries), 'block_rows': block_rows, 'block_starts': starts,
            'selection': 'evenly spaced contiguous source blocks, source order within blocks'}
