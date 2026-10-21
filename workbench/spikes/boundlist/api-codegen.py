#!/usr/bin/env python3
"""Compare linked complete-consumer instruction shapes, retaining diagnostics."""
import argparse
import collections
import gzip
import json
import re
from pathlib import Path


def functions(path):
    result = {}
    current = None
    with gzip.open(path, 'rt') as stream:
        for line in stream:
            heading = re.match(r'^[0-9a-f]+ <(.+)>:', line)
            if heading:
                current = heading[1].replace('ikea::boundlist::sidecar::', 'ikea::boundlist::')
                result[current] = []
            elif current and re.match(r'^\s*[0-9a-f]+:', line):
                pieces = line.rstrip().split('\t')
                if len(pieces) < 2:
                    continue
                instruction = '\t'.join(pieces[1:]).strip()
                instruction = re.sub(r'0x[0-9a-f]+ <(.+)>', r'<\1>', instruction)
                instruction = instruction.replace('ikea::boundlist::sidecar::', 'ikea::boundlist::')
                result[current].append(instruction)
    return {name: code for name, code in result.items() if 'chase<' in name or 'consume<' in name}


def compare(before, after):
    rows = []
    for name in sorted(before.keys() & after.keys()):
        a, b = before[name], after[name]
        hist = lambda code: dict(collections.Counter(i.split()[0] for i in code))
        outlined = lambda code: sum(bool(re.match(r'^(bl|callq?)\b', i)) and
                                     any(term in i for term in ('geometry_reverse', 'geometry_overlap',
                                         'boundlist::reverse<', 'boundlist::overlap<', 'rank_selection::'))
                                     for i in code)
        rows.append(dict(function=name, before_instructions=len(a), after_instructions=len(b),
                         same_instruction_text=a == b, same_opcode_histogram=hist(a) == hist(b),
                         before_outlined_geometry_calls=outlined(a), after_outlined_geometry_calls=outlined(b)))
    return dict(note='Text removes linked symbol target addresses and old namespace; it is not a machine-byte or cycle-equivalence proof.',
                common_functions=len(rows), identical_text=sum(r['same_instruction_text'] for r in rows),
                identical_opcode_histograms=sum(r['same_opcode_histogram'] for r in rows),
                after_outlined_geometry_calls=sum(r['after_outlined_geometry_calls'] for r in rows), functions=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = compare(functions(args.before), functions(args.after))
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print({k: v for k, v in report.items() if k != 'functions'})
