"""Window the retained source ZIPs, preserving bitmap lineage and source order."""
from array import array
import hashlib
import json
import re
import shutil
import struct
import sys
from typing import cast
import zipfile

DEFAULTS = {'min_cardinality': 1}


def integers(stream):
    """Read comma/newline-delimited decimal u32s without materializing a bitmap."""
    tail = b''
    while chunk := stream.read(65536):
        parts = re.split(rb'[,\s]+', tail + chunk)
        tail = parts.pop()
        for token in parts:
            if token:
                if not token.isdigit():
                    raise ValueError('Expected unsigned decimal row ID')
                yield int(token)
    if tail:
        if not tail.isdigit():
            raise ValueError('Expected unsigned decimal row ID')
        yield int(tail)


def prepare(sources, output, min_cardinality):
    if type(min_cardinality) is not int or not 1 <= min_cardinality <= 65536:
        raise ValueError('min_cardinality must be an integer in [1, 65536]')
    summary = {}
    with (output / 'bitmaps.jsonl').open('w') as origins:
        for source_name, path in sorted(sources.items()):
            if not source_name.endswith('.zip'):
                shutil.copyfile(path, output / source_name)
                continue
            name = source_name[:-4]
            # Related sorted variants and dimension projections share a family.
            family = 'dimension' if name.startswith('dimension_') else name.removesuffix('_srt')
            stats = {'bitmaps': 0, 'duplicate_files': 0, 'windows': 0,
                     'positions': 0, 'omitted_windows': 0, 'family': family}
            seen = {}
            with zipfile.ZipFile(path) as archive, (output / (name + '.kw16')).open('wb') as data, \
                    (output / (name + '.windows.jsonl')).open('w') as index:
                for member in sorted(n for n in archive.namelist() if n.endswith('.txt')):
                    with archive.open(member) as source:
                        # Read-mode ZipFile.open returns ZipExtFile; the broader
                        # IO[bytes] stub does not expose its readinto method.
                        raw_hash = hashlib.file_digest(cast(zipfile.ZipExtFile, source), 'sha256').hexdigest()
                    duplicate = seen.get(raw_hash)
                    origin = {'dataset': name, 'family': family, 'member': member,
                              'bitmap_id': raw_hash, 'duplicate_of': duplicate}
                    origins.write(json.dumps(origin, sort_keys=True) + '\n')
                    if duplicate is not None:
                        stats['duplicate_files'] += 1
                        continue
                    seen[raw_hash] = member
                    stats['bitmaps'] += 1
                    high = None
                    positions = array('H')
                    previous = -1

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
                                                'bitmap_id': raw_hash}, sort_keys=True) + '\n')
                        stats['windows'] += 1
                        stats['positions'] += len(positions)

                    with archive.open(member) as source:
                        for value in integers(source):
                            if not previous < value < 2**32:
                                raise ValueError(f'{name}/{member}: IDs must be strictly increasing u32s')
                            previous = value
                            if high is not None and value >> 16 != high:
                                emit()
                                positions = array('H')
                            high = value >> 16
                            positions.append(value & 65535)
                    if high is not None:
                        emit()
            summary[name] = stats
    # The historical converter discarded cardinalities below 16. Validate its
    # recorded output where available, without executing or importing that code.
    checked = []
    if min_cardinality == 16:
        for line in (output / 'MANIFEST.sha256').read_text().splitlines():
            checksum, filename = line.split()
            if filename.endswith('.kw16'):
                with (output / filename).open('rb') as f:
                    actual = hashlib.file_digest(f, 'sha256').hexdigest()
                if actual != checksum:
                    raise ValueError(f'Historical window bytes differ: {filename}')
                checked.append(filename)
    return {'datasets': summary, 'historical_kw16_verified': checked,
            'min_cardinality': min_cardinality, 'window_bits': 65536,
            'empty_windows': 'not synthesized; source bitmap universe is unspecified'}
