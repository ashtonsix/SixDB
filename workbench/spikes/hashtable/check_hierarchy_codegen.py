#!/usr/bin/env python3
"""Inventory complete scalar/4/8 read callers, including calls and stack traffic."""
import argparse
import gzip
import json
import re
from pathlib import Path


def check(path):
    opener = gzip.open if path.suffix == '.gz' else open
    header = re.compile(r'^[0-9a-f]+ <(.+)>:$')
    instruction = re.compile(r'^\s*[0-9a-f]+:\s+(\S+)\s*(.*)$')
    roots = {}
    current = None
    with opener(path, 'rt') as stream:
        for line in stream:
            match = header.match(line.strip())
            if match:
                name = match[1]
                current = name if name.startswith('ht::hierarchy_packet::answer ht::hierarchy_packet::read<') else None
                if current is not None:
                    roots[current] = []
            elif current is not None and instruction.match(line):
                roots[current].append(line.strip())
    assert len(roots) == 9, ('expected three owners at scalar/4/8', len(roots))
    result = []
    identities = set()
    for name, body in roots.items():
        n = int(re.search(r'ht::hierarchy_packet::read<(\d+)u', name)[1])
        owner = ('exact' if 'baseline<true>' in name else
                 'ordinary' if 'baseline<false>' in name else 'candidate')
        identities.add((owner, n))
        calls = [line for line in body if re.search(r'\s(?:callq?|bl|blr)\s', line)]
        stack = [line for line in body if re.search(r'(?:\bsp\b|%rsp|%rbp)', line)]
        multiplies = [line for line in body if re.search(r'\s(?:umull2?|umlal2?|vpmuludq)\s', line)]
        # Read fusion is a comparison property, not a demand for frameless code.
        # Writes retain real allocation/copy calls and are not checked here.
        assert not calls, (name, calls)
        result.append(dict(owner=owner, N=n, symbol=name, instructions=len(body),
                           calls=calls, stack_references=len(stack),
                           explicit_widening_multiplies=len(multiplies)))
    assert identities == {(o, n) for o in ('candidate', 'ordinary', 'exact') for n in (1, 4, 8)}
    return dict(consumers=result, read_calls=0,
                limitation='Static caller inventory only. Stack references are not dynamic spills; '
                           'instruction counts do not predict throughput, latency or memory service.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path)
    print(json.dumps(check(parser.parse_args().assembly), indent=2))
