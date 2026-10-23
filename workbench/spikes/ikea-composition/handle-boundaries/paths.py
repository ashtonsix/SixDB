#!/usr/bin/env python3
"""Trace one specified static fast path in the complete maintained caller.

This does not execute or model timing. The selected path is a nonterminal
independent8 iteration, full activity, tight placement and in-domain RHS. It
keeps all actual predicates and addresses in the receipt and refuses an
unexpected control-flow shape instead of following a speculative fallback.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess


def target(row):
    return int(re.search(r'0x([0-9a-f]+) <', row)[1], 16)


def trace(path, profile):
    record = json.loads(path.read_text())
    name, body = [(n, b) for n, b in record['functions'].items()
                  if n.startswith('void runtime::measure<') and 'Tuple<5u' in n][0]
    raw = name.rsplit('[symbol:', 1)[1][:-1]
    command = ['llvm-objdump-21', '-d', '-r', '--line-numbers',
               '--disassemble-symbols=' + raw, str(path.with_suffix('').with_suffix('.o'))]
    process = subprocess.run(command, capture_output=True, text=True, check=True)
    assert not process.stderr, process.stderr
    path.with_name('measure5-lines.s').write_text(process.stdout)
    rows = {}
    for line in process.stdout.splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s|[0-9a-f]{8}\s)', line)
        if match:
            rows[int(match[1], 16)] = line.split('\t', 1)[1]
    addresses = sorted(rows)
    following = dict(zip(addresses, addresses[1:]))
    neon = profile == 'neon-v2'
    timer = 'R_AARCH64_CALL26' if neon else 'R_X86_64_PLT32'
    calls = [int(m[1], 16) - (0 if neon else 1) for m in re.finditer(
        r'^\s*([0-9a-f]+):\s+' + timer + r'\s+_ZNSt6chrono3_V212steady_clock3nowEv',
        process.stdout, re.M)]
    assert len(calls) == 2, calls
    # The second now() begins the timed loop. Its mode branch selects the
    # independent8 initialization, which jumps to the complete loop header.
    candidates = [(a, r) for a, r in rows.items() if a > calls[1]][:14]
    choices = [(a, r) for a, r in candidates if r.startswith('tbz\t' if neon else 'je\t')]
    assert len(choices) == (1 if neon else 2), choices
    initial = target(choices[-1][1])
    setup = [r for a, r in rows.items() if a >= initial][:7]
    jumps = [r for r in setup if r.startswith('b\t' if neon else 'jmp\t')]
    assert len(jumps) == 1, setup
    start = target(jumps[0])
    address, flags, walked, conditions = start, '', [], []
    while not walked or address != start:
        assert len(walked) < 1000, (path, hex(address))
        row = rows[address]
        walked.append((hex(address), row))
        mnemonic = row.split('\t', 1)[0]
        conditional = ((mnemonic.startswith('b.') or mnemonic in ('cbz', 'cbnz', 'tbz', 'tbnz'))
                       if neon else mnemonic.startswith('j'))
        if mnemonic.startswith(('cmp', 'test')):
            flags = row
        if mnemonic == ('b' if neon else 'jmp'):
            address = target(row)
        elif conditional:
            full = bool(re.search(r'^(?:cmpw\t\$(?:-0x1|-1),|cmpl\t\$0xffff,)', flags))
            taken = not neon and mnemonic == 'je' and full
            conditions.append({'address': hex(address), 'instruction': row,
                               'last_test': flags, 'taken': taken})
            address = target(row) if taken else following[address]
        else:
            address = following[address]
    counts = Counter(row.split('\t', 1)[0] for _, row in walked)
    assert len(conditions) == 33, conditions
    assert counts['cmhs' if neon else ('vpcmpgtb' if profile == 'avx2-zen5' else 'vpcmpleub')] == 8, counts
    path.with_name('tight-full-independent8-path.s').write_text(
        '\n'.join(a + ': ' + r for a, r in walked) + '\n')
    return {'scope': __doc__, 'objdump_command': command, 'function': name,
            'literal_function_sha256': hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest(),
            'entry': hex(start), 'instructions': len(walked), 'opcodes': dict(counts),
            'conditional_branches': conditions, 'trace': walked}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = {}
    for path in sorted(args.output.rglob('extra_tuple_2.record.json')):
        profile, arm = path.parent.parent.name, path.parent.name
        mode = 'configured-release' if 'configured-release' in path.relative_to(args.output).parts else 'standalone'
        result['/'.join((mode, profile, arm))] = trace(path, profile)
    (args.output / 'tight-paths.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
