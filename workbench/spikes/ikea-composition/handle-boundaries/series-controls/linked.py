#!/usr/bin/env python3
"""Verify the measured SeriesPack complete callers and shared tables offline.

Instruction counts are static symbol extents, including cold/unissued paths;
they are not dynamic work or a timing model. Actual payload bytes are checked
against the ELF, using the existing composition-study ELF reader.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('linked_elf', HERE.parent/'linked-filter.py')
linked = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(linked)
ROUTES = ['columns', 'shared-fresh', 'shared-retained', 'copied-fresh', 'copied-retained', 'typed']
OPS = ['point', 'range', 'read16']
STRIPED = {1, 2, 3, 4, 5, 6, 7, 10, 12, 14, 15, 20}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def role(name):
    route = re.search(r'::traverse<\(series_controls::\(anonymous namespace\)::route\)([0-5]), \(series_controls::operation\)([0-2])>', name)
    if route:
        return 'read/'+ROUTES[int(route[1])]+'/'+OPS[int(route[2])]
    init = re.search(r'::traverse_initialize<\(series_controls::\(anonymous namespace\)::init_route\)([0-2])>', name)
    if init:
        return 'initialize/'+['columns', 'dispatch', 'typed'][int(init[1])]
    if '::queries<(series_controls::operation)2,' in name:
        return 'read16_queries/'+('shared' if 'series_controls::shared_reader<' in name else 'copied')
    if name.startswith('std::expected<ikea::seriespack::erased_reader<unsigned long>,') and 'dense_input const&' in name:
        return 'copied_binder'
    if 'series_controls::initialize_dispatch(' in name:
        return 'initialize_dispatch'
    if 'series_controls::initialized(' in name:
        return 'effect_sink'


def inspect(root, profile, output):
    receipt = json.loads((root/'receipt.json').read_text())
    assert receipt['complete'] and receipt['source_unchanged']
    binary = root/profile/'ikea_series_controls_commands'
    dump = root/profile/'series-linked.txt'
    assert sha(binary) == receipt['binaries'][profile]['series']['sha256']
    command = next(c for c in receipt['commands'] if Path(c['log']).name == 'series-linked.txt'
                   and profile in Path(c['log']).parts)
    assert sha(dump) == command['log_sha256'] and command['returncode'] == 0
    elf = linked.Elf(binary)
    arm = struct.unpack_from('<H', elf.data, 18)[0] == 183
    labels, functions, current = {}, {}, None
    for line in dump.read_text().splitlines():
        header = re.match(r'^([0-9a-f]+) <(.*)>:$', line)
        if header:
            address, name = int(header[1], 16), header[2]
            labels[address] = name
            try:
                size = elf.function_size(address)
            except AssertionError:
                current = None
                continue
            current = {'address': address, 'name': name, 'bytes': size, 'rows': []}
            functions[address] = current
        elif current:
            pattern = r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s*\t(.*)' if arm else r'^\s*([0-9a-f]+):\s+([0-9a-f]{2}(?: [0-9a-f]{2})*)\s*\t(.*)'
            row = re.match(pattern, line)
            if row and int(row[1], 16) < current['address']+current['bytes']:
                raw = bytes.fromhex(row[2].replace(' ', ''))
                if arm:
                    raw = raw[::-1]
                current['rows'].append((int(row[1], 16), raw, row[3]))

    def record(address):
        function = functions[address]
        raw = b''.join(row[1] for row in function['rows'])
        assert len(raw) == function['bytes'] and raw == elf.read(address, len(raw))
        calls, backedges = [], []
        for place, _, assembly in function['rows']:
            mnemonic = assembly.split('\t')[0]
            if mnemonic in ('bl', 'blr', 'callq', 'call'):
                calls.append({'address': hex(place), 'assembly': assembly})
            match = re.search(r'\b0x([0-9a-f]+) <', assembly)
            if match and int(match[1], 16) < place and (mnemonic.startswith(('j', 'b.')) or mnemonic in ('b', 'cbz', 'cbnz', 'tbz', 'tbnz')):
                backedges.append({'address': hex(place), 'assembly': assembly})
        return {'name': function['name'], 'address': hex(address), 'bytes': len(raw),
                'sha256': hashlib.sha256(raw).hexdigest(), 'static_instructions': len(function['rows']),
                'calls': calls, 'backedges': backedges,
                'prologue': [row[2] for row in function['rows'][:12]]}

    selected = {role(f['name']): record(a) for a, f in functions.items() if role(f['name'])}
    assert len([name for name in selected if name.startswith(('read/', 'initialize/'))]) == 21
    symbols = {s['name']: s for s in elf.symbols if s['type'] == 1}
    tables, targets = {}, set()
    for suffix, bits in [('h', 8), ('t', 16), ('j', 32), ('m', 64)]:
        symbol = symbols[f'_ZN15series_controls12read_columnsI{suffix}EE']
        assert symbol['size'] == 3072
        pointers = struct.unpack('<384Q', elf.read(symbol['address'], symbol['size']))
        used = 0
        for op_index, operation in enumerate(OPS):
            for index in range(128):
                width, geometry = index % 64+1, index // 64
                valid = width <= bits and (not geometry or width in STRIPED)
                address = pointers[op_index*128+index]
                assert bool(address) == valid
                if valid:
                    name = labels[address]
                    assert f'::dense_{operation}<' in name
                    assert f'layout<{width}u, (ikea::seriespack::geometry){geometry}, 0u>' in name
                    targets.add(address); used += 1
                    if bits == 64 and width in (7, 31) and (not geometry or width == 7):
                        selected[f'native_u64/{operation}/k{width}/'+('striped' if geometry else 'local')] = record(address)
        tables['read_u'+str(bits)] = {'address': hex(symbol['address']), 'bytes': symbol['size'],
                                      'sha256': hashlib.sha256(elf.read(symbol['address'], symbol['size'])).hexdigest(),
                                      'nonzero_slots': used, 'zero_slots': 384-used}
    for name, mangled in [('typed', '_ZN15series_controls13typed_columnsE'),
                          ('initialize', '_ZN15series_controls18initialize_columnsE'),
                          ('typed_initialize', '_ZN15series_controls24typed_initialize_columnsE')]:
        symbol = symbols[mangled]
        assert symbol['size'] == 1024
        pointers = struct.unpack('<128Q', elf.read(symbol['address'], symbol['size']))
        for index, address in enumerate(pointers):
            width, geometry = index % 64+1, index // 64
            assert bool(address) == (not geometry or width in STRIPED)
            if address:
                assert f'::{name}<' in labels[address]
                targets.add(address)
                if width in (7, 31) and (not geometry or width == 7):
                    selected[f'{name}/k{width}/'+('striped' if geometry else 'local')] = record(address)
        tables[name] = {'address': hex(symbol['address']), 'bytes': symbol['size'],
                        'sha256': hashlib.sha256(elf.read(symbol['address'], symbol['size'])).hexdigest(),
                        'nonzero_slots': 76, 'zero_slots': 52}
    symbol = symbols['_ZN15series_controls14extent_columnsE']
    assert symbol['size'] == 512
    data = elf.read(symbol['address'], symbol['size'])
    for index in range(128):
        width, geometry = index % 64+1, index // 64
        tile, shift, padding = struct.unpack_from('<HBB', data, index*4)
        if not geometry or width in STRIPED:
            rows = 32*(8//math.gcd(width % 8, 8)) if geometry else 8
            assert tile == rows*width//8 and 1 << shift == rows
        else:
            assert not tile and not shift
        assert not padding
    tables['extent'] = {'address': hex(symbol['address']), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    assert sum(table['bytes'] for table in tables.values()) == 15872
    for key, value in selected.items():
        function = functions[int(value['address'], 16)]
        path = output/(profile+'-'+key.replace('/', '-')+'.s')
        path.write_text('\n'.join(f'{a:x}: {raw.hex()}\t{assembly}' for a, raw, assembly in function['rows'])+'\n')
    commands = json.loads((root/profile/'compile_commands.json').read_text())
    compile_commands = [c for c in commands if '/series-controls/' in c['file']]
    assert len(compile_commands) == 5
    for c in compile_commands:
        assert '-O3 -g -std=c++23' in c['command'] and '-DNDEBUG' not in c['command']
    return {'binary_sha256': sha(binary), 'binary_bytes': binary.stat().st_size,
            'disassembly_sha256': sha(dump), 'receipt_sha256': sha(root/'receipt.json'),
            'compile_commands': compile_commands, 'tables': tables, 'functions': selected,
            'unique_table_target_symbols': len(targets),
            'nontransitive_table_target_symbol_bytes': sum(elf.function_size(a) for a in targets),
            'text_section_bytes': next(s[5] for n, s in zip(elf.names, elf.sections) if n == '.text')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='member=collected results directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=False)
    result = {'scope': __doc__, 'helper_sha256': sha(Path(__file__)),
              'elf_parser_sha256': sha(HERE.parent/'linked-filter.py'), 'profiles': {}}
    for named in args.inputs:
        _, directory = named.split('=', 1); root = Path(directory)
        for profile in json.loads((root/'receipt.json').read_text())['profiles']:
            assert profile not in result['profiles']
            result['profiles'][profile] = inspect(root, profile, args.output)
    (args.output/'receipt.json').write_text(json.dumps(result, indent=2, sort_keys=True)+'\n')
    print(json.dumps({p: {'verified_functions': len(r['functions']), 'text_section_bytes': r['text_section_bytes']}
                      for p, r in result['profiles'].items()}, indent=2))


if __name__ == '__main__':
    main()
