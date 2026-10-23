#!/usr/bin/env python3
"""Inspect complete linked R4 Tuple filter routes and their addressed payloads.

This is a static audit of the maintained benchmark ELFs, not a timing model.
ELF symbol extents exclude inter-function padding. The explicitly selected
bound/stage path has nonempty full activity, an in-domain RHS and unit stride.
All its conditions remain recorded; unknown control flow fails the audit.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Elf:
    def __init__(self, path):
        self.data = path.read_bytes()
        assert self.data[:6] == b'\x7fELF\x02\x01', path
        header = struct.unpack_from('<16sHHIQQQIHHHHHH', self.data)
        offset, size, count, strings = header[6], header[11], header[12], header[13]
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', self.data, offset + i * size)
                         for i in range(count)]
        names = self.sections[strings]
        names = self.data[names[4]:names[4] + names[5]]
        self.names = [names[s[0]:].split(b'\0', 1)[0].decode() for s in self.sections]
        self.symbols = []
        for section in self.sections:
            if section[1] not in (2, 11):
                continue
            strings = self.sections[section[6]]
            strings = self.data[strings[4]:strings[4] + strings[5]]
            for start in range(section[4], section[4] + section[5], section[9]):
                name, info, other, index, value, length = struct.unpack_from('<IBBHQQ', self.data, start)
                self.symbols.append({'name': strings[name:].split(b'\0', 1)[0].decode(),
                    'type': info & 15, 'index': index, 'address': value, 'size': length})

    def section(self, address):
        matches = [(n, s) for n, s in zip(self.names, self.sections)
                   if s[2] & 2 and s[3] <= address < s[3] + s[5]]
        assert len(matches) == 1, hex(address)
        return matches[0]

    def read(self, address, length):
        name, section = self.section(address)
        assert address + length <= section[3] + section[5], (name, hex(address), length)
        if section[1] == 8:
            return bytes(length)
        offset = section[4] + address - section[3]
        return self.data[offset:offset + length]

    def function_size(self, address):
        sizes = {s['size'] for s in self.symbols
                 if s['address'] == address and s['type'] == 2 and s['size']}
        assert len(sizes) == 1, (hex(address), sizes)
        return sizes.pop()

    def payload(self, address, instruction):
        name, section = self.section(address)
        symbols = [s for s in self.symbols if s['address'] == address and s['type'] == 1 and s['size']]
        mnemonic = instruction.split('\t', 1)[0]
        if symbols:
            sizes = {s['size'] for s in symbols}
            assert len(sizes) == 1
            length, kind = sizes.pop(), 'complete object initial bytes'
        elif mnemonic == 'leaq' and name == '.rodata':
            available = min(4096, section[3] + section[5] - address)
            data = self.read(address, available)
            length, kind = data.index(0) + 1, 'complete nul-terminated string'
        elif mnemonic in ('vmovsd', 'vdivsd', 'vmulsd', 'vaddsd', 'vsubsd'):
            length, kind = 8, 'scalar double operand'
        elif '%zmm' in instruction:
            length, kind = 64, 'full vector operand'
        elif '%ymm' in instruction:
            length, kind = 32, 'full vector operand'
        elif '%xmm' in instruction:
            length, kind = 16, 'full vector operand'
        else:
            raise ValueError(('unclassified payload', hex(address), instruction))
        payload = self.read(address, length)
        return {'address': hex(address), 'section': name, 'size': length, 'kind': kind,
                'bytes': payload.hex(), 'sha256': digest(payload),
                'symbols': sorted(s['name'] for s in symbols)}


def role(name):
    tuple_type = re.search(r'extras::Tuple<(5|8)u, \(ikea::comparison\)([0-4])>', name)
    if tuple_type:
        width, op = tuple_type.groups()
        if name.startswith('unsigned short extras::bound<'):
            return f'w{width}/op{op}/bound'
        if '::stage<' in name and 'extras::one<' in name:
            return f'w{width}/op{op}/one_stage'
        if name.startswith('void runtime::measure<') and 'run_extra<' in name:
            return f'w{width}/op{op}/inline_measure'
    if name.startswith('unsigned short extras::entry<1u>'):
        return 'common/entry1'
    if name.startswith('void runtime::measure<extras::measure_bound_extra('):
        return 'common/indirect_timer'
    if '::stage<' in name and 'extras::decode<extras::TupleDecode>' in name:
        return 'shared/native_decode'
    joined = re.search(r'extras::joined<1u, false, \(ikea::comparison\)([0-4])>', name)
    if '::stage<' in name and joined:
        return 'shared/joined/op' + joined[1]


def functions(path, elf):
    result, current = {}, None
    for line in path.read_text().splitlines():
        header = re.match(r'^([0-9a-f]+) <(.*)>:$', line)
        if header:
            current = None
            key = role(header[2])
            if key:
                assert key not in result, key
                address = int(header[1], 16)
                current = {'name': header[2], 'address': address,
                           'size': elf.function_size(address), 'rows': []}
                result[key] = current
        elif current:
            match = re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]{2}(?: [0-9a-f]{2})*)\s*\t(.*)', line)
            if match and int(match[1], 16) < current['address'] + current['size']:
                current['rows'].append((int(match[1], 16), match[2].replace(' ', ''), match[3]))
    for value in result.values():
        actual = b''.join(bytes.fromhex(row[1]) for row in value['rows'])
        assert len(actual) == value['size'], (value['name'], len(actual), value['size'])
        assert actual == elf.read(value['address'], value['size'])
    return result


def trace(function):
    rows = {a: instruction for a, _, instruction in function['rows']}
    addresses = list(rows)
    following = dict(zip(addresses, addresses[1:]))
    address, flags, walked, conditions = function['address'], '', [], []
    while True:
        assert len(walked) < 200, (function['name'], len(walked), hex(address))
        row = rows[address]
        walked.append([hex(address), row])
        mnemonic = row.split('\t', 1)[0]
        if mnemonic.startswith(('cmp', 'test')):
            flags = row
        if mnemonic == 'retq' or (mnemonic in ('jmp', 'jmpq') and row.split('\t', 1)[1].startswith('*')):
            break
        if mnemonic in ('jmp', 'jmpq'):
            address = int(re.search(r'0x([0-9a-f]+) <', row)[1], 16)
        elif mnemonic.startswith('j'):
            full = bool(re.search(r'^(?:cmpw\t\$(?:-0x1|-1),|cmpl\t\$0xffff,)', flags))
            taken = mnemonic == 'je' and full
            # Actual path has exactly empty, domain, full and stride guards.
            assert full or flags.startswith(('test', 'cmp')), (row, flags)
            conditions.append({'address': hex(address), 'instruction': row,
                               'last_test': flags, 'taken': taken})
            address = int(re.search(r'0x([0-9a-f]+) <', row)[1], 16) if taken else following[address]
        else:
            address = following[address]
    assert len(conditions) == 4, (function['name'], conditions)
    return {'instructions': len(walked), 'opcodes': dict(Counter(r.split('\t', 1)[0] for _, r in walked)),
            'conditions': conditions, 'trace': walked}


def trace_inline(function):
    rows = {a: instruction for a, _, instruction in function['rows']}
    addresses = list(rows)
    following = dict(zip(addresses, addresses[1:]))
    timers = [a for a, r in rows.items() if 'callq' in r and '_ZNSt6chrono3_V212steady_clock3nowEv' in r]
    assert len(timers) == 2, (function['name'], timers)
    candidates = [(a, r) for a, r in rows.items() if a > timers[1]][:14]
    branches = [(a, r) for a, r in candidates if r.startswith('je\t')]
    assert len(branches) == 2, (function['name'], candidates)
    initial = int(re.search(r'0x([0-9a-f]+) <', branches[-1][1])[1], 16)
    setup = [r for a, r in rows.items() if a >= initial][:7]
    jumps = [r for r in setup if r.startswith('jmp\t')]
    assert len(jumps) == 1, (function['name'], setup)
    start = int(re.search(r'0x([0-9a-f]+) <', jumps[0])[1], 16)
    address, flags, walked, conditions = start, '', [], []
    while not walked or address != start:
        assert len(walked) < 1000, (function['name'], len(walked), hex(address))
        row = rows[address]
        walked.append([hex(address), row])
        mnemonic = row.split('\t', 1)[0]
        if mnemonic.startswith(('cmp', 'test')):
            flags = row
        if mnemonic in ('jmp', 'jmpq'):
            address = int(re.search(r'0x([0-9a-f]+) <', row)[1], 16)
        elif mnemonic.startswith('j'):
            full = bool(re.search(r'^(?:cmpw\t\$(?:-0x1|-1),|cmpl\t\$0xffff,)', flags))
            taken = mnemonic == 'je' and full
            conditions.append({'address': hex(address), 'instruction': row,
                               'last_test': flags, 'taken': taken})
            address = int(re.search(r'0x([0-9a-f]+) <', row)[1], 16) if taken else following[address]
        else:
            address = following[address]
    counts = Counter(r.split('\t', 1)[0] for _, r in walked)
    assert len(conditions) == 33, (function['name'], conditions)
    assert sum(n for op, n in counts.items() if op.startswith('vpcmp')) == 8, counts
    return {'scope': 'Nonterminal independent8 full-active tight in-domain iteration; existing index opacity unchanged.',
        'instructions': len(walked), 'opcodes': dict(counts), 'conditions': conditions, 'trace': walked}


def compact(result):
    retained = {'scope': result['scope'], 'runner_sha256': result['runner_sha256'],
        'limits': ['Selected actual R4 linked benchmark functions, not every filter or arbitrary caller.',
            'Same opcode counts do not establish equal scheduling, dependencies, fetch/branch behavior or timing.',
            'ELF payload equality verifies initial allocated bytes at addressed targets; it does not prove runtime state.',
            'Full symbol-sized bodies and explicit path traces remain in ignored output; hashes retain identity.',
            'Shared route bypasses the prepared encoded predicate; one_stage uses the same predicate as bound/inline.'],
        'profiles': {}}
    for profile, pair in result['profiles'].items():
        item = {'inputs': pair['inputs'], 'functions': {'columns': [
            'route', 'before_address', 'after_address', 'before_size', 'after_size',
            'before_instructions', 'after_instructions', 'before_sha256', 'after_sha256', 'raw_bytes_equal'], 'rows': []},
            'paths': {'columns': ['route', 'kind', 'before_instructions', 'after_instructions',
                'before_conditions', 'after_conditions', 'opcode_differences', 'before_trace_sha256', 'after_trace_sha256'], 'rows': []},
            'timer_payloads': pair['timer_payloads'], 'shared_payloads': pair['shared_payloads'],
            'path_payloads': {},
            'all_changed_timer_payloads_equal': pair['all_changed_timer_payloads_equal'],
            'all_changed_shared_payloads_equal': pair['all_changed_shared_payloads_equal']}
        for key, f in pair['functions'].items():
            b, a = f['before'], f['after']
            for arm in ('before', 'after'):
                for payload in f[arm].get('path_payloads', []):
                    item['path_payloads'][arm + '/' + payload['address']] = payload
            item['functions']['rows'].append([key, hex(b['address']), hex(a['address']), b['size'], a['size'],
                b['instructions'], a['instructions'], b['bytes_sha256'], a['bytes_sha256'], f['raw_function_bytes_equal']])
            for kind in ('full_tight_in_domain_path', 'full_tight_in_domain_independent8_path'):
                if kind not in b:
                    continue
                old, new = b[kind], a[kind]
                diff = {op: [old['opcodes'].get(op, 0), new['opcodes'].get(op, 0)]
                    for op in sorted(set(old['opcodes']) | set(new['opcodes']))
                    if old['opcodes'].get(op, 0) != new['opcodes'].get(op, 0)}
                item['paths']['rows'].append([key, kind, old['instructions'], new['instructions'],
                    len(old['conditions']), len(new['conditions']), diff,
                    digest(json.dumps(old, sort_keys=True).encode()), digest(json.dumps(new, sort_keys=True).encode())])
        retained['profiles'][profile] = item
    return retained


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compact', type=Path)
    args = parser.parse_args()
    result = {'scope': __doc__, 'runner_sha256': digest(Path(__file__).read_bytes()), 'profiles': {}}
    for profile in ('avx2', 'avx512'):
        arms = {}
        elfs = {}
        for arm in ('before', 'after'):
            folder = args.results / profile / arm
            binary, disassembly = folder / 'filter_composition_extra', folder / 'filters-disassembly.txt'
            elf = Elf(binary)
            elfs[arm] = elf
            arms[arm] = functions(disassembly, elf)
        assert arms['before'].keys() == arms['after'].keys()
        pair = {'inputs': {arm: {'elf_sha256': digest(elfs[arm].data),
            'disassembly_sha256': digest((args.results / profile / arm / 'filters-disassembly.txt').read_bytes())}
            for arm in arms}, 'functions': {}, 'timer_payloads': [], 'shared_payloads': []}
        for key in arms['before']:
            row = {}
            for arm in arms:
                f = arms[arm][key]
                row[arm] = {k: f[k] for k in ('name', 'address', 'size')}
                row[arm].update({'bytes_sha256': digest(elfs[arm].read(f['address'], f['size'])),
                    'instructions': len(f['rows']),
                    'opcodes': dict(Counter(r.split('\t', 1)[0] for _, _, r in f['rows']))})
                if key.endswith(('/bound', '/one_stage')):
                    row[arm]['full_tight_in_domain_path'] = trace(f)
                elif key.endswith('/inline_measure'):
                    row[arm]['full_tight_in_domain_independent8_path'] = trace_inline(f)
            row['raw_function_bytes_equal'] = row['before']['bytes_sha256'] == row['after']['bytes_sha256']
            if key.startswith('shared/'):
                old, new = arms['before'][key], arms['after'][key]
                assert len(old['rows']) == len(new['rows'])
                for b, a in zip(old['rows'], new['rows']):
                    assert b[0] - old['address'] == a[0] - new['address']
                    if b[1] == a[1]:
                        continue
                    assert '(%rip)' in b[2] and '(%rip)' in a[2], (key, b, a)
                    def skeleton(instruction):
                        return re.sub(r'[-]?0x[0-9a-f]+\(%rip\)', 'ADDRESS(%rip)', instruction.split('#', 1)[0]).strip()
                    assert skeleton(b[2]) == skeleton(a[2]), (key, b, a)
                    payloads = {arm: elfs[arm].payload(int(re.search(r'# 0x([0-9a-f]+)', r[2])[1], 16), r[2])
                                for arm, r in (('before', b), ('after', a))}
                    pair['shared_payloads'].append({'function': key, 'offset': b[0] - old['address'],
                        'before_instruction': b[2], 'after_instruction': a[2], **payloads,
                        'payload_equal': payloads['before']['bytes'] == payloads['after']['bytes']})
            pair['functions'][key] = row
        before = arms['before']['common/indirect_timer']['rows']
        after = arms['after']['common/indirect_timer']['rows']
        assert len(before) == len(after)
        for old, new in zip(before, after):
            if old == new:
                continue
            assert old[0] == new[0] and '(%rip)' in old[2] and '(%rip)' in new[2], (old, new)
            # Keep literal bytes/targets; only verify that this operand moved.
            def skeleton(row):
                return re.sub(r'[-]?0x[0-9a-f]+\(%rip\)', 'ADDRESS(%rip)', row.split('#', 1)[0]).strip()
            assert skeleton(old[2]) == skeleton(new[2]), (old, new)
            payloads = {}
            for arm, instruction in (('before', old), ('after', new)):
                address = int(re.search(r'# 0x([0-9a-f]+)', instruction[2])[1], 16)
                payloads[arm] = elfs[arm].payload(address, instruction[2])
            pair['timer_payloads'].append({'instruction_address': hex(old[0]),
                'before_instruction': old[2], 'after_instruction': new[2], **payloads,
                'payload_equal': payloads['before']['bytes'] == payloads['after']['bytes']})
        pair['all_changed_timer_payloads_equal'] = all(r['payload_equal'] for r in pair['timer_payloads'])
        pair['all_changed_shared_payloads_equal'] = all(r['payload_equal'] for r in pair['shared_payloads'])
        # All directly addressed data on each selected bound/stage path is
        # retained too; this includes the AVX2 unsigned comparison sign mask.
        for key, f in pair['functions'].items():
            if not key.endswith(('/bound', '/one_stage')):
                continue
            for arm in arms:
                f[arm]['path_payloads'] = [elfs[arm].payload(int(re.search(r'# 0x([0-9a-f]+)', r)[1], 16), r)
                    for _, r in f[arm]['full_tight_in_domain_path']['trace'] if '(%rip)' in r]
        result['profiles'][profile] = pair
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    if args.compact:
        args.compact.parent.mkdir(parents=True, exist_ok=True)
        args.compact.write_text(json.dumps(compact(result), indent=2, sort_keys=True) + '\n')
    for profile, pair in result['profiles'].items():
        print(profile, 'functions', len(pair['functions']), 'timer payloads', len(pair['timer_payloads']),
              'equal', pair['all_changed_timer_payloads_equal'])


if __name__ == '__main__':
    main()
