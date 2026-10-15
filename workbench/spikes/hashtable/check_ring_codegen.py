#!/usr/bin/env python3
"""Qualify ready callbacks and ordinary Abseil consumers before Ring timing.

Input is demangled llvm-objdump -d output, optionally gzip-compressed. This
bounded panel emits nine geometries times four hints and six baseline arms.
--legacy requires exactly the preceding seven geometries; it never silently
accepts a missing new root in a current capture.
"""
import argparse
import gzip
import json
import re
from pathlib import Path


HEADER = re.compile(r'^[0-9a-f]+ <(.+)>:$')
INSTRUCTION = re.compile(
    r'^\s*[0-9a-f]+:\s+(?:(?:[0-9a-f]{8}|[0-9a-f]{2})\s+)+'
    r'([a-z][a-z0-9_.]*)\s*(.*)$')
BASELINE = re.compile(r'ht::direct<(1|8|32)u, (false|true), ht::abseil<40u>')
READY = re.compile(r'ht::visit<\(loom::Hint\)([0-3]), ht::table<40u, (\d+)u, (\d+)u, (\d+)u, (\d+)u, false, false(?:, 8u)?>::reader>')
OLD_GEOMETRIES = {(3, 0, 64, 64), (8, 7, 64, 128), (8, 4, 128, 128),
                  (14, 13, 64, 128), (14, 10, 128, 128), (19, 18, 64, 128), (19, 15, 128, 128)}
DRAM_GEOMETRIES = {(23, 22, 64, 128), (23, 19, 128, 128)}
STACK_REGISTER = re.compile(r'\b(?:sp|wsp|rsp|esp)\b|\[%?x29\b|\(%?rbp\b')


def ordinary_call(mnemonic):
    return mnemonic in ('call', 'callq', 'bl', 'lcall', 'lcallq') or mnemonic.startswith('blr')


def permitted_outer_call(operands):
    # The C32 local caller outlines zero-initialization and final reduction.
    # No lookup/helper call is permitted through this allow-list.
    return (re.search(r'<(?:memset|bzero)(?:@[^>]*)?>', operands) is not None or
            '<ht::answer ht::result<std::array<ht::lane, ' in operands)


def check_assembly(text, legacy=False):
    bodies = {}
    current = None
    for line in text.splitlines():
        header = HEADER.match(line.strip())
        if header:
            name = header[1]
            current = name if 'ht::visit<' in name or BASELINE.search(name) else None
            if current is not None:
                if current in bodies:
                    raise ValueError(f'duplicate consumer symbol: {current}')
                bodies[current] = []
        elif current is not None:
            instruction = INSTRUCTION.match(line)
            if instruction:
                bodies[current].append((instruction[1], instruction[2], line.strip()))

    ready = {name: body for name, body in bodies.items() if 'ht::visit<' in name}
    baseline = {name: body for name, body in bodies.items() if BASELINE.search(name)}
    variants = {BASELINE.search(name).groups() for name in baseline}
    expected = {(str(c), prefetch) for c in (1, 8, 32) for prefetch in ('false', 'true')}
    geometries = OLD_GEOMETRIES | (set() if legacy else DRAM_GEOMETRIES)
    expected_ready = {(hint, *shape) for shape in geometries for hint in range(4)}
    ready_variants = {tuple(map(int, match.groups())) for name in ready if (match := READY.search(name))}
    if len(ready) != len(expected_ready) or ready_variants != expected_ready or len(baseline) != 6 or variants != expected:
        raise ValueError(f'expected exact {len(expected_ready)} ready callbacks and six C1/8/32 baseline arms; '
                         f'got {len(ready)}, {len(baseline)}, {sorted(variants)}')

    failures = {}
    allowed = {}
    for name, body in bodies.items():
        bad = []
        outer = []
        if not body:
            bad.append('no decoded instructions; unsupported or incomplete disassembly')
        for mnemonic, operands, line in body:
            if name in ready:
                if ordinary_call(mnemonic) or STACK_REGISTER.search(operands.split(' <', 1)[0]) or \
                        mnemonic.startswith(('push', 'pop')) or mnemonic in ('enter', 'leave', 'leaveq'):
                    bad.append(line)
            else:
                if ordinary_call(mnemonic):
                    (outer if permitted_outer_call(operands) else bad).append(line)
                # Also reject a tail jump to outlined lookup. Other baseline
                # branches may name this enclosing consumer (ht::abseil).
                elif '<' in operands and re.search(r'absl::.*::(?:find|prefetch)(?:<|\()', operands):
                    bad.append(line)
        if bad:
            failures[name] = bad
        if outer:
            allowed[name] = outer
    if failures:
        raise ValueError(json.dumps(failures, indent=2))
    return dict(ready_callbacks=len(ready), baseline_consumers=len(baseline),
                geometries=sorted(geometries), legacy=legacy,
                ready_calls=0, ready_stack_references=0, outlined_baseline_lookup=0,
                permitted_outer_calls=allowed,
                limitation='Checks static calls and stack references, not instruction quality, '
                           'hint-window validity, source lifetime, traffic or performance. '
                           'Permitted initialization/reduction sites still require source review.')


def check(path, legacy=False):
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as stream:
        return check_assembly(stream.read(), legacy)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path)
    parser.add_argument('--legacy', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.assembly, args.legacy), indent=2))
    except ValueError as error:
        parser.exit(1, f'Ring codegen guard failed: {error}\n')
