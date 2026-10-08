#!/usr/bin/env python3
"""Inspect actual callback and always-inline hint targets on pinned primary ISAs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(path, target):
    output = subprocess.check_output(['/usr/bin/llvm-objdump-21', '-d', '--no-show-raw-insn', str(path)], text=True)
    path.with_suffix('.asm').write_text(output)
    bodies = {}
    current = None
    for line in output.splitlines():
        match = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if match:
            current = match[1]
            bodies[current] = []
        elif current:
            match = re.match(r'^\s*[0-9a-f]+:\s+(\S+)\s*(.*)', line)
            if match:
                bodies[current].append((match[1], match[2]))
    result = []
    for name, instructions in bodies.items():
        callback = name.startswith('loom_policy_')
        hint = '5entryILNS_9ProcedureE' in name
        if not callback and not hint:
            continue
        calls = [op + ' ' + args for op, args in instructions if op in ('call', 'callq', 'bl', 'blr')]
        stack = [op + ' ' + args for op, args in instructions
                 if (target == 'v2' and re.search(r'\bsp\b', args)) or
                 (target == 'zen5' and (op.startswith(('push', 'pop')) or re.search(r'%rsp', args)))]
        hints = [op for op, args in instructions if op == 'prfm' or op.startswith('prefetch')]
        conditional = [op + ' ' + args for op, args in instructions
                       if (target == 'v2' and (op.startswith('b.') or op in ('cbz', 'cbnz', 'tbz', 'tbnz'))) or
                       (target == 'zen5' and op.startswith('j') and op not in ('jmp', 'jmpq'))]
        tails = [op + ' ' + args for op, args in instructions
                 if op in ('br', 'jmp', 'jmpq') and re.match(r'(?:x\d+|\*)', args)]
        result.append({'body': name, 'kind': 'callback' if callback else 'hint',
                       'instructions': len(instructions), 'calls': calls, 'stack': stack,
                       'hint_instructions': len(hints), 'conditional_branches': conditional,
                       'indirect_branches': tails})
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    study = root / 'workbench/spikes/loom-hardware-policy'
    common = ['-std=c++23', '-O3', '-g0', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
              '-ffp-contract=off', '-fno-fast-math', '-I' + str(root / 'loom/include'), '-I' + str(root / 'orbital/include')]
    targets = {'v2': ['-mcpu=neoverse-v2'],
               'zen5': ['--target=x86_64-linux-gnu', '--sysroot=/usr/x86_64-linux-gnu',
                        '-isystem', '/usr/x86_64-linux-gnu/include/c++/15', '-isystem',
                        '/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu', '-march=znver5']}
    receipt = {'recipe_sha256': sha(Path(__file__).resolve()),
               'compiler': subprocess.check_output(['/usr/bin/clang++-21', '--version'], text=True),
               'source_sha256': {str(path.relative_to(root)): sha(path) for path in
                                 [study / 'native.cpp', study / 'native.h', study / 'policy.h', study / 'policy.cpp',
                                  root / 'loom/include/loom/ring.h']},
               'qualification': 'Static object code; no hardware timing or executed-instruction claim', 'targets': {}}
    for target, flags in targets.items():
        entry = {'objects': [], 'bodies': []}
        for source in ('native.cpp', 'policy.cpp'):
            object_path = output / f'{target}-{Path(source).stem}.o'
            command = ['/usr/bin/clang++-21'] + flags + common + ['-c', str(study / source), '-o', str(object_path)]
            subprocess.run(command, check=True)
            entry['objects'].append({'argv': command, 'sha256': sha(object_path)})
            entry['bodies'] += inspect(object_path, target)
        inline = [p for p in entry['bodies'] if p['kind'] == 'callback' and not p['body'].endswith(('_procedure', '_switch'))]
        procedures = [p for p in entry['bodies'] if p['kind'] == 'callback' and p['body'].endswith('_procedure')]
        switches = [p for p in entry['bodies'] if p['kind'] == 'callback' and p['body'].endswith('_switch')]
        hints = [p for p in entry['bodies'] if p['kind'] == 'hint']
        assert len(inline) == 18 and all(not p['calls'] and not p['stack'] and len(p['indirect_branches']) == 1 for p in inline)
        assert len(procedures) == 2 and all(len(p['calls']) == 1 and p['stack'] for p in procedures)
        assert len(switches) == 2 and all(not p['calls'] and not p['stack'] for p in switches)
        assert len(hints) == 9 and all(not p['calls'] and not p['stack'] and not p['conditional_branches'] for p in hints)
        entry['passed'] = True
        receipt['targets'][target] = entry
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS: V2/Zen5 inline switch has no ordinary call/frame; dynamic procedure pointer retained as control')


if __name__ == '__main__':
    main()
