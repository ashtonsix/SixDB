#!/usr/bin/env python3
"""Qualify Loom ready callbacks and their paired ordinary consumers.

Reads demangled llvm-objdump -d output, optionally gzip-compressed. The
qualification is static: it does not establish instruction quality, source
lifetime, memory traffic or performance. Its JSON format preserves the native
receipts from this spike's initial and followup campaigns.
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
BASELINE = re.compile(
    r'ht::staged_test::direct<(1|32)u, (false|true), ht::staged_test::baseline_reader>')
READY = re.compile(r'^void ht::staged_test::visit<(false|true), ')
STACK_REGISTER = re.compile(r'\b(?:sp|wsp|rsp|esp)\b|\[%?x29\b|\(%?rbp\b')
OUTLINED_LOOKUP = re.compile(r'absl::.*::(?:find|prefetch)(?:<|\()')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def ordinary_call(mnemonic):
    return mnemonic in ('call', 'callq', 'bl', 'lcall', 'lcallq') or mnemonic.startswith('blr')


def permitted_outer_call(operands):
    # The ordinary C32 consumer may outline initialization and final reduction.
    # Lookup and per-request helper calls are deliberately outside this list.
    return (re.search(r'<(?:memset|bzero)(?:@[^>]*)?>', operands) is not None or
            re.search(r'ht::staged_test::(?:initialize|result)<', operands) is not None)


def check_assembly(text, word_control=False):
    bodies = {}
    current = None
    for line in text.splitlines():
        header = HEADER.match(line.strip())
        if header:
            name = header[1]
            current = name if name.startswith('void ht::staged_test::visit<') or BASELINE.search(name) else None
            if current is not None:
                require(current not in bodies, f'duplicate consumer symbol: {current}')
                bodies[current] = []
        elif current is not None:
            instruction = INSTRUCTION.match(line)
            if instruction:
                bodies[current].append((instruction[1], instruction[2], line.strip()))

    ready = {name: body for name, body in bodies.items() if name.startswith('void ht::staged_test::visit<')}
    baseline = {name: body for name, body in bodies.items() if BASELINE.search(name)}
    owners = ('generic', 'word') if word_control else ('generic',)
    ready_variants = set()
    for name in ready:
        match = READY.match(name)
        require(match is not None, f'unrecognised ready callback: {name}')
        ready_variants.add(('word' if 'word_reader<' in name else 'generic', match[1]))
    expected_ready = {(owner, hint) for owner in owners for hint in ('false', 'true')}
    require(len(ready) == len(expected_ready) and ready_variants == expected_ready,
            f'expected exact {len(expected_ready)} ready callbacks; got {len(ready)}: {sorted(ready_variants)}')
    variants = {BASELINE.search(name).groups() for name in baseline}
    expected_baseline = {(str(concurrency), hint) for concurrency in (1, 32) for hint in ('false', 'true')}
    require(len(baseline) == 4 and variants == expected_baseline,
            f'expected four C1/32 baseline consumers; got {len(baseline)}: {sorted(variants)}')

    consumers = []
    failures = {}
    saved_callbacks = 0
    for name, body in bodies.items():
        require(bool(body), f'no decoded instructions: {name}')
        bad, allowed, callee_save = [], [], []
        for mnemonic, operands, line in body:
            if name in ready:
                # preserve_none still preserves RBP on x86. Accept and report
                # the explicit push and return-path pops; other stack use or
                # ordinary calls remain a failed ready-callback qualification.
                if mnemonic in ('pushq', 'popq') and operands.strip() == '%rbp':
                    callee_save.append(line)
                elif (ordinary_call(mnemonic) or STACK_REGISTER.search(operands.split(' <', 1)[0]) or
                      mnemonic.startswith(('push', 'pop')) or mnemonic in ('enter', 'leave', 'leaveq')):
                    bad.append(line)
            elif ordinary_call(mnemonic):
                (allowed if permitted_outer_call(operands) else bad).append(line)
            elif '<' in operands and OUTLINED_LOOKUP.search(operands):
                # A tail branch to an outlined lookup is still per-request work.
                bad.append(line)
        if bad:
            failures[name] = bad
        if callee_save:
            require(sum('pushq' in line for line in callee_save) == 1,
                    f'expected one RBP save in ready callback: {name}')
            require(any('popq' in line for line in callee_save),
                    f'missing RBP restore in ready callback: {name}')
            saved_callbacks += 1
        consumers.append(dict(owner='word' if 'word_reader<' in name else 'generic' if name in ready else 'abseil',
                              ready=name in ready, hints='visit<true' in name, instructions=len(body),
                              allowed_outer_calls=allowed, rbp_preservation=callee_save))
    require(not failures, json.dumps(failures, indent=2))
    return dict(consumers=consumers, ready_calls=0, ready_other_stack_references=0,
                ready_callbacks_preserving_rbp=saved_callbacks, outlined_baseline_lookup=0,
                limitation='Static calls and frame references only; not instruction quality, lifetime, traffic or performance.')


def check(path, word_control=False):
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as stream:
        return check_assembly(stream.read(), word_control)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path)
    parser.add_argument('--word-control', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.assembly, args.word_control), indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, f'Loom consumer codegen guard failed: {error}\n')


if __name__ == '__main__':
    main()
