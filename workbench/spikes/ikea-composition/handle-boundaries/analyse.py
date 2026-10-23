#!/usr/bin/env python3
"""Extract literal complete-function observations from this study's objects.

Instruction counts and stack adjustments are descriptive, not dynamic costs.
The ELF records retain the exact bytes, relocation targets and addends; this
summary hashes them without normalizing control-specific names or literals.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def functions(path):
    result = {}
    current = None
    for line in path.read_text().splitlines():
        match = re.match(r'^([0-9a-f]+) <(.*)>:$', line)
        if match:
            current = match[2]
            result[current] = []
        if current and re.match(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s|[0-9a-f]{8}\s)', line):
            assert '\t' in line, line
            result[current].append(line.split('\t', 1)[1])
    return result


def stack_adjustment(row):
    row = row.split(' //', 1)[0].split(' # imm', 1)[0].rstrip()
    return bool(re.match(r'(pushq|popq)\b', row) or
                re.match(r'(subq|addq|andq)\t\$[^,]+, %rsp$', row) or
                re.match(r'(movq|leaq)\t.*, %rsp$', row) or
                re.match(r'(sub|add)\tsp, sp, #', row) or
                re.match(r'(and|mov)\tsp, ', row) or
                ('[sp' in row and (']!' in row or '], #' in row)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = {}
    for path in sorted(args.output.rglob('*.record.json')):
        profile, arm = path.parent.parent.name, path.parent.name
        if 'configured-release' in path.relative_to(args.output).parts:
            profile = 'configured-release/' + profile
        record = json.loads(path.read_text())
        assembly = functions(path.with_name(path.name.replace('.record.json', '.s')))
        entries = {}
        for name, body in record['functions'].items():
            signature = name.split(' [symbol:', 1)[0]
            if not (signature.startswith('hb::') or 'run_extra<' in signature or
                    signature.startswith('void extras::run_tuple<')):
                continue
            rows = assembly[signature]
            if profile == 'neon-v2':
                assert len(rows) * 4 == body['size'], (name, len(rows), body['size'])
            canonical = json.dumps(body, sort_keys=True, separators=(',', ':')).encode()
            entries[name] = {
                'bytes': body['size'], 'instructions': len(rows),
                'literal_record_sha256': hashlib.sha256(canonical).hexdigest(),
                'relocations': body['relocations'],
                'stack_adjustments': [row for row in rows if stack_adjustment(row)],
                'first': rows[:24],
            }
        result.setdefault(profile, {}).setdefault(arm, {})[path.stem.replace('.record', '')] = entries
    (args.output / 'assembly-observations.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
