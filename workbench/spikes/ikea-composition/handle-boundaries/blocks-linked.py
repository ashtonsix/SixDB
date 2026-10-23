#!/usr/bin/env python3
"""Revalidate the annotated linked ARM block-command witness, without building.

This deliberately admits only the qualified R4/cached-tight ELF pair. Source
meaning and the store-forwarding hypothesis are manual annotations, not a
general instruction-cost model. Exact hashes, symbol extents, disassembly,
selected branch paths and query loops are checked against the actual ELFs.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent
EVIDENCE = HERE.parent / 'evidence/interface-cohesion-v2-20261023/predicate-blocks-linked-arm.json'
SPEC = importlib.util.spec_from_file_location('linked_filter', HERE / 'linked-filter.py')
linked = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(linked)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(results):
    expected = json.loads(EVIDENCE.read_text())
    receipt = json.loads((results / 'receipt.json').read_text())
    manifest = json.loads((results / 'source-manifest.json').read_text())
    assert digest(results / 'receipt.json') == expected['receipt_sha256']
    assert receipt['profiles'] == ['neon'] and receipt['preflight_complete'] and receipt['complete']
    assert 'clang version 21.1.8 ' in (results / 'compiler.txt').read_text()
    assert receipt['source_manifest_sha256'] == expected['source_manifest_sha256']
    assert receipt['candidate_sha256'] == expected['candidate_header_sha256']
    assert receipt['shared_source_sha256'][expected['source']] == expected['source_sha256']
    old, new = manifest['source_sha256']['before'], manifest['source_sha256']['after']
    assert old.keys() == new.keys()
    assert [name for name in old if old[name] != new[name]] == ['ikea/include/ikea/tuplepack/filter.h']
    verified = {}
    for arm, record in expected['arms'].items():
        folder = results / 'neon' / arm
        binary, disassembly = folder / 'ikea_predicate_blocks', folder / 'blocks-disassembly.txt'
        assert digest(binary) == record['binary_sha256']
        assert digest(disassembly) == record['disassembly_sha256']
        elf = linked.Elf(binary)  # Existing linked-ELF parser; virtual addresses are not ET_REL offsets.
        assert struct.unpack_from('<H', elf.data, 18)[0] == 183  # EM_AARCH64
        text = disassembly.read_text()
        labels = {int(a, 16): name for a, name in re.findall(r'^([0-9a-f]+) <(.*)>:$', text, re.M)}
        instructions = {int(a, 16): (word, assembly) for a, word, assembly in
                        re.findall(r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(.+)', text, re.M)}
        commands = json.loads((folder / 'compile_commands.json').read_text())
        command = next(c['command'] for c in commands if c['file'].endswith('/blocks.cpp'))
        assert command == record['compile_command']
        assert '-O3 -g -std=c++23' in command and '-DNDEBUG' not in command
        assert '-march=armv8-a+simd -mtune=neoverse-v2' in command
        checked = 0
        for function in record['functions']:
            start, size = int(function['address'], 16), function['size']
            assert labels[start] == function['name'] and elf.function_size(start) == size
            assert hashlib.sha256(elf.read(start, size)).hexdigest() == function['sha256']
            assert size == function['instructions'] * 4
            for address in range(start, start + size, 4):
                word, _ = instructions[address]
                assert elf.read(address, 4) == bytes.fromhex(word)[::-1]
                checked += 1
        for loop in expected['query_loops'][arm].values():
            start, end = int(loop['start'], 16), int(loop['backedge'], 16)
            assert (end - start) // 4 + 1 == loop['static_instructions_including_call']
            body = [instructions[a][1] for a in range(start, end + 1, 4)]
            assert sum(s.startswith('bl\t') and 'predicate<64u' in s for s in body) == 1
            assert body[-1].startswith('b.ne\t0x' + format(start, 'x') + ' ')
        selected = record['checked_full_tight_path']
        path = selected['trace']
        assert len(path) == selected['instruction_count']
        for index, row in enumerate(path):
            address = int(row['address'], 16)
            assembly = instructions[address][1]
            assert assembly == row['assembly']
            mnemonic = assembly.split('\t')[0]
            if mnemonic == 'ret':
                assert index + 1 == len(path)
                continue
            following = int(path[index + 1]['address'], 16)
            if mnemonic == 'b' or mnemonic.startswith('b.') or mnemonic in ('cbz', 'cbnz', 'tbz', 'tbnz'):
                target = int(re.search(r'\b0x([0-9a-f]+)', assembly)[1], 16)
                assert following in ({target} if mnemonic == 'b' else {target, address + 4})
            else:
                assert following == address + 4
        assert sum('cmhs\t' in row['assembly'] for row in path) == selected['vector_comparisons'] == 1
        assert sum(row['assembly'].startswith('ldr\tq0,') for row in path) == selected['vector_payload_loads'] == 1
        assert sum(row['assembly'].split('\t')[0].startswith('b.') or row['assembly'].split('\t')[0]
                   in ('cbz', 'cbnz', 'tbz', 'tbnz') for row in path) == selected['conditional_branches'] == 7
        constant = elf.read(int(selected['movemask_constant_address'], 16), 16).hex()
        assert constant == selected['movemask_constant_bytes']
        verified[arm] = {'function_symbols': len(record['functions']), 'literal_instructions': checked}
    return {'evidence_sha256': digest(EVIDENCE), 'parser_sha256': digest(HERE / 'linked-filter.py'),
            'helper_sha256': digest(Path(__file__)), 'verified': verified,
            'limits': 'Exact annotated ARM pair only; no new timing or causal inference.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.results)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
