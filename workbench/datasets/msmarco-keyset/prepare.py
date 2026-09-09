"""Stream the retained Boolean term postings into windows with term/range lineage."""
from array import array
import json
import shutil
import struct
import sys

DEFAULTS = {'min_cardinality': 1}


def prepare(sources, output, min_cardinality):
    if type(min_cardinality) is not int or not 1 <= min_cardinality <= 65536:
        raise ValueError('min_cardinality must be an integer in [1, 65536]')
    summary = {}
    for filename, path in sorted(sources.items()):
        if not filename.endswith('.u32'):
            shutil.copyfile(path, output / filename)
            continue
        term = filename[:-4]
        stats = {'windows': 0, 'positions': 0, 'source_positions': 0, 'omitted_windows': 0}
        with path.open('rb') as source, (output / (term + '.kw16')).open('wb') as data, \
                (output / (term + '.windows.jsonl')).open('w') as index:
            high = None
            previous = -1
            positions = array('H')

            def emit():
                if len(positions) < min_cardinality:
                    stats['omitted_windows'] += 1
                    return
                offset = data.tell()
                data.write(struct.pack('<I', len(positions)))
                if sys.byteorder != 'little':
                    positions.byteswap()
                data.write(positions.tobytes())
                index.write(json.dumps({'ordinal': stats['windows'], 'offset': offset,
                                        'cardinality': len(positions), 'high16': high,
                                        'term': term}, sort_keys=True) + '\n')
                stats['windows'] += 1
                stats['positions'] += len(positions)

            while chunk := source.read(65536):
                if len(chunk) % 4:
                    raise ValueError(f'{filename}: truncated u32')
                for (value,) in struct.iter_unpack('<I', chunk):
                    if value <= previous:
                        raise ValueError(f'{filename}: IDs must be strictly increasing')
                    previous = value
                    stats['source_positions'] += 1
                    if high is not None and value >> 16 != high:
                        emit()
                        positions = array('H')
                    high = value >> 16
                    positions.append(value & 65535)
            if high is not None:
                emit()
        summary[term] = stats
    return {'terms': summary, 'min_cardinality': min_cardinality, 'window_bits': 65536,
            'semantics': 'Boolean document membership; no term frequencies or document lengths',
            'empty_windows': 'not synthesized; gaps remain recoverable from high16'}
