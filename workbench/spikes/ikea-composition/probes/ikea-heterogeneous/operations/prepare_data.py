"""Small, source-aligned pair samples; each record is A8192 | B8192 | C8192.

C supplies a nonempty-slice candidate mask, not an exact third predicate. Pair
coverage is deliberately balanced and does not represent query frequencies.
Only selected source windows are expanded; whole corpus indices stay compact.
"""
from collections import Counter, defaultdict
import json
from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import datasets

WINDOW_BYTES = 8192
RECORD_BYTES = 3 * WINDOW_BYTES
MSMARCO_DOCUMENTS = 8841823
SEED = 'ikea-heterogeneous-operations-20260910'


def family(name):
    return 'dimension' if name.startswith('dimension_') else name.removesuffix('_srt')


def partition(name):
    return {'census1881': 'validation_comparison',
            'census-income': 'test_comparison'}.get(name, 'train')


def indices(directory, names, identity_field):
    """One source ID can have many windows; ordinals never pair source lists."""
    by_source = defaultdict(dict)
    for name in names:
        with (directory / (name + '.windows.jsonl')).open() as stream:
            for line in stream:
                row = json.loads(line)
                source_id = row[identity_field]
                if row['high16'] in by_source[source_id]:
                    raise ValueError('Duplicate source/window coordinate')
                by_source[source_id][row['high16']] = (name, row)
    return dict(by_source)


def select_roaring(by_source, rng):
    """Four joint and four one-sided windows, balanced over eligible high16s."""
    source_ids = sorted(by_source)
    present = defaultdict(list)
    for source_id in source_ids:
        for high in by_source[source_id]:
            present[high].append(source_id)
    joint = sorted(high for high, ids in present.items() if len(ids) >= 2)
    one_sided = sorted(high for high, ids in present.items() if len(ids) < len(source_ids))
    if not joint or not one_sided:
        raise ValueError('This recipe expects both strata in each retained Roaring archive')
    selected = []
    seen = set()
    for stratum in ['joint'] * 4 + ['one_sided'] * 4:
        for attempt in range(1000):
            high = rng.choice(joint if stratum == 'joint' else one_sided)
            if stratum == 'joint':
                left, right = rng.sample(present[high], 2)
            else:
                left = rng.choice(present[high])
                absent = sorted(set(source_ids) - set(present[high]))
                right = rng.choice(absent)
                # Exercise absence on both sides rather than always one operand.
                if len(selected) % 2:
                    left, right = right, left
            key = (min(left, right), max(left, right), high)
            if key not in seen:
                seen.add(key)
                selected.append((left, right, high, stratum))
                break
        else:
            raise ValueError('Could not choose eight distinct source/window pairs')
    return selected


def select_msmarco(by_source, rng):
    """A seeded matching uses all 16 available terms once as a primary operand."""
    terms = sorted(by_source)
    if len(terms) != 16:
        raise ValueError('This recipe expects the retained 16-term MS MARCO sample')
    rng.shuffle(terms)
    selected = []
    for left, right in zip(terms[::2], terms[1::2]):
        highs = sorted(by_source[left].keys() | by_source[right].keys())
        high = rng.choice(highs)
        stratum = 'joint' if high in by_source[left] and high in by_source[right] else 'one_sided'
        selected.append((left, right, high, stratum))
    return selected


def read_window(directory, operand, high):
    row = operand['row']
    if row is None:
        return bytes(WINDOW_BYTES)
    if row['high16'] != high:
        raise ValueError('Operand does not belong to the selected coordinate window')
    with (directory / operand['window_file']).open('rb') as stream:
        stream.seek(row['offset'])
        header = stream.read(4)
        if len(header) != 4:
            raise ValueError('Truncated source cardinality')
        population, = struct.unpack('<I', header)
        if population != row['cardinality']:
            raise ValueError('Source index/cardinality mismatch')
        encoded = stream.read(population * 2)
    if len(encoded) != population * 2:
        raise ValueError('Truncated source positions')
    data = bytearray(WINDOW_BYTES)
    previous = -1
    for (position,) in struct.iter_unpack('<H', encoded):
        if position <= previous:
            raise ValueError('Source positions are not strictly increasing')
        if operand['dataset'] == 'msmarco-keyset' and (high << 16 | position) >= MSMARCO_DOCUMENTS:
            raise ValueError('Document ID exceeds the declared MS MARCO universe')
        previous = position
        data[position // 8] |= 1 << (position % 8)
    if sum(byte.bit_count() for byte in data) != population:
        raise ValueError('Expanded source population mismatch')
    return bytes(data)


def get():
    sources = {name: datasets.get(name) for name in ('real-roaring', 'msmarco-keyset')}
    metadata = {name: datasets.verify(directory) for name, directory in sources.items()}
    identities = {name: meta['id'] for name, meta in metadata.items()}
    if any(meta['details']['min_cardinality'] != 1 for meta in metadata.values()):
        raise ValueError('Pairing requires all nonempty source windows, including sparse windows')

    def build(out):
        roaring_origins = {}
        with (sources['real-roaring'] / 'bitmaps.jsonl').open() as stream:
            for line in stream:
                origin = json.loads(line)
                if origin['duplicate_of'] is None:
                    roaring_origins[(origin['dataset'], origin['bitmap_id'])] = origin
        groups = []
        for path in sorted(sources['real-roaring'].glob('*.kw16')):
            groups.append(('real-roaring', path.stem, [path.stem], 'bitmap_id'))
        marco_terms = sorted(path.stem for path in sources['msmarco-keyset'].glob('*.kw16'))
        groups.append(('msmarco-keyset', 'msmarco', marco_terms, 'term'))
        lineage = {'format': 1, 'seed': SEED, 'inputs': identities,
                   'record_bytes': RECORD_BYTES, 'operand_order': ['left', 'right', 'candidate'],
                   'mask_semantics': 'nonempty 256-position slices of candidate; explicit query restriction only',
                   'weighting': 'eight pairs per archive; balanced strata, not occurrence or query frequency',
                   'predictor_partition_note': 'Original frozen predictor partitions; all comparison families were previously inspected',
                   'groups': {}}
        for dataset, name, names, identity_field in groups:
            by_source = indices(sources[dataset], names, identity_field)
            rng = random.Random(SEED + '/' + name)
            pairs = select_roaring(by_source, rng) if dataset == 'real-roaring' else select_msmarco(by_source, rng)
            source_ids = sorted(by_source)
            group_family = family(name)
            records = []
            with (out / (name + '.pairs')).open('wb') as output:
                for ordinal, (left, right, high, stratum) in enumerate(pairs):
                    remaining = [source_id for source_id in source_ids if source_id not in (left, right)]
                    present = [source_id for source_id in remaining if high in by_source[source_id]]
                    candidate = rng.choice(present or remaining)
                    operands = {}
                    populations = {}
                    candidate_slices = 0
                    for role, source_id in zip(('left', 'right', 'candidate'), (left, right, candidate)):
                        source_name = name if dataset == 'real-roaring' else source_id
                        found = by_source[source_id].get(high)
                        row = found[1] if found else None
                        source_file = source_name + ('.zip' if dataset == 'real-roaring' else '.u32')
                        operand = {'dataset': dataset, 'prepared_id': identities[dataset],
                                   'source_file': source_file,
                                   'source_file_sha256': metadata[dataset]['request']['inputs'][source_file],
                                   identity_field: source_id, 'window_file': source_name + '.kw16',
                                   'row': row, 'high16': high,
                                   'absence': None if row else 'No nonempty window at this high16 in the complete source index'}
                        if dataset == 'real-roaring':
                            operand['origin'] = roaring_origins[(name, source_id)]
                        data = read_window(sources[dataset], operand, high)
                        output.write(data)
                        populations[role] = sum(byte.bit_count() for byte in data)
                        if role == 'candidate':
                            candidate_slices = sum(any(data[start:start + 32]) for start in range(0, WINDOW_BYTES, 32))
                        operands[role] = operand
                    records.append({'pair_id': f'{name}/{ordinal:02d}', 'ordinal': ordinal,
                                    'offset': ordinal * RECORD_BYTES, 'high16': high,
                                    'stratum': stratum, 'operands': operands,
                                    'populations': populations, 'candidate_nonempty_slices': candidate_slices})
            lineage['groups'][name] = {
                'dataset': dataset, 'archive_family': group_family,
                'original_predictor_partition': partition(group_family),
                'coordinate_domain': (f'archive {name}: original row IDs; window selected from operand union'
                                      if dataset == 'real-roaring' else 'MS MARCO document IDs [0, 8841823)'),
                'sorted_variant': name.endswith('_srt'), 'records': records}
        (out / 'lineage.json').write_text(json.dumps(lineage, indent=2, sort_keys=True) + '\n')
        return validate(out, sources)

    return datasets.cached('ikea-heterogeneous-operation-pairs', Path(__file__), identities, {}, build)


def validate(prepared, sources=None):
    """Rejoin each recorded source row/absence and verify every emitted bit."""
    lineage = json.loads((Path(prepared) / 'lineage.json').read_text())
    if sources is None:
        sources = {name: datasets.get(name) for name in lineage['inputs']}
    if any(datasets.verify(sources[name])['id'] != identity for name, identity in lineage['inputs'].items()):
        raise ValueError('Validation source identity differs from recorded input')
    summary = {}
    for name, group in lineage['groups'].items():
        dataset = group['dataset']
        names = [name] if dataset == 'real-roaring' else sorted(p.stem for p in sources[dataset].glob('*.kw16'))
        field = 'bitmap_id' if dataset == 'real-roaring' else 'term'
        original = indices(sources[dataset], names, field)
        payload = (Path(prepared) / (name + '.pairs')).read_bytes()
        if len(payload) != len(group['records']) * RECORD_BYTES:
            raise ValueError('Pair payload has the wrong extent')
        counts = Counter()
        for index, record in enumerate(group['records']):
            high = record['high16']
            if record['ordinal'] != index or record['offset'] != index * RECORD_BYTES:
                raise ValueError('Pair ordinal/offset mismatch')
            ids = [record['operands'][role][field] for role in lineage['operand_order']]
            if len(set(ids)) != 3:
                raise ValueError('Each pair must use three distinct same-domain source lists')
            counts[record['stratum']] += 1
            for slot, role in enumerate(lineage['operand_order']):
                operand = record['operands'][role]
                found = original[operand[field]].get(high)
                if operand['row'] != (found[1] if found else None):
                    raise ValueError('Recorded source row or absence does not rejoin')
                data = read_window(sources[dataset], operand, high)
                start = record['offset'] + slot * WINDOW_BYTES
                if payload[start:start + WINDOW_BYTES] != data:
                    raise ValueError('Pair bytes differ from source positions')
                if sum(byte.bit_count() for byte in data) != record['populations'][role]:
                    raise ValueError('Recorded population mismatch')
                if role == 'candidate':
                    selected = sum(any(data[start:start + 32]) for start in range(0, WINDOW_BYTES, 32))
                    if selected != record['candidate_nonempty_slices']:
                        raise ValueError('Recorded candidate slice mask count mismatch')
                    counts['candidate_nonempty'] += selected > 0
                    counts['candidate_nonempty_slices'] += selected
            nonempty = sum(record['populations'][role] > 0 for role in ('left', 'right'))
            if nonempty != (2 if record['stratum'] == 'joint' else 1):
                raise ValueError('Pair does not satisfy its selection stratum')
            counts['left_absent'] += record['populations']['left'] == 0
            counts['right_absent'] += record['populations']['right'] == 0
        summary[name] = {'pairs': len(group['records']), 'bytes': len(payload), **dict(counts)}
    return {'source_ids': lineage['inputs'], 'sampling': lineage['weighting'], 'groups': summary}


if __name__ == '__main__':
    prepared = get()
    print(prepared)
    if '--check' in sys.argv[1:]:
        print(json.dumps(validate(prepared), indent=2, sort_keys=True))
