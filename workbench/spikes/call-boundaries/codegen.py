#!/usr/bin/env python3
"""Capture caller and callee code for this spike; no hardware jobs or timing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(binary, destination, architecture):
    assembly = subprocess.check_output(
        ['llvm-objdump-21', '-dr', '--demangle', '--no-show-raw-insn', str(binary)], text=True)
    destination.write_text(assembly)
    functions = {}
    current = None
    for line in assembly.splitlines():
        match = re.match(r'^[0-9a-f]+ <(.*)>:', line)
        if match:
            current = match[1]
            functions[current] = []
        elif current:
            if 'R_AARCH64_' in line or 'R_X86_64_' in line:
                if functions[current]:
                    functions[current][-1] += ' [relocation: ' + line.strip() + ']'
                continue
            match = re.match(r'^\s*[0-9a-f]+:\s+(\S+)\s*(.*)', line)
            if match:
                functions[current].append(match[1] + ' ' + match[2])
    result = []
    symbols = subprocess.check_output(['llvm-nm-21', '-S', '--demangle', '--radix=d',
                                      str(binary)], text=True)
    sizes = {}
    for line in symbols.splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) == 4 and fields[0].isdigit() and fields[1].isdigit():
            sizes[fields[3]] = int(fields[1])
    for name, body in functions.items():
        if not any(prefix in name for prefix in ('cb::', 'loom::', 'orbital::')):
            continue
        calls = [line for line in body if re.match(r'^(bl|blr|call|callq) ', line)]
        x86_frame = any(re.match(r'movq? %rsp, %rbp', line) for line in body)
        stack = [line for line in body if
                 (architecture == 'arm' and re.search(r'\b(sp|x29)\b', line)) or
                 (architecture == 'x86' and (re.search(r'%rsp\b', line) or
                                          (x86_frame and re.search(r'%rbp\b', line)) or
                                          re.match(r'^(push|pop)(q|l|w)? ', line)))]
        result.append({'name': name, 'bytes': sizes.get(name), 'instructions': len(body), 'calls': calls,
                       'stack_instructions': stack, 'first': body[:12], 'last': body[-10:]})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse', type=Path,
                        help='Reinspect an existing compile receipt into a new directory')
    args = parser.parse_args()
    root, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if args.reuse:
        previous = json.loads((args.reuse / 'receipt.json').read_text())
        previous['analyzer_sha256'] = sha(Path(__file__))
        previous['compiled_receipt_sha256'] = sha(args.reuse / 'receipt.json')
        for target, entries in previous['targets'].items():
            arch = 'arm' if target == 'v2' else 'x86'
            for entry in entries:
                obj = Path(entry['argv'][-1])
                if sha(obj) != entry['object_sha256']:
                    raise ValueError('Object changed: ' + str(obj))
                entry['functions'] = inspect(obj, output / (target + '-' + obj.stem + '.asm'), arch)
        (output / 'receipt.json').write_text(json.dumps(previous, indent=2) + '\n')
        print('Reinspected hash-verified objects; no recompilation')
        return
    study = Path('workbench/spikes/call-boundaries')
    sources = [study / name for name in ('returns.cpp', 'callers.cpp', 'ring.cpp')]
    sources += [Path(name) for name in ('loom/src/worker.cpp', 'loom/src/ring.cpp',
        'orbital/src/allocation/allocation.cpp', 'orbital/src/allocation/group.cpp',
        'orbital/src/allocation/window.cpp', 'orbital/src/worker.cpp')]
    common = ['-std=c++23', '-O3', '-g0', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
              '-ffp-contract=off', '-fno-fast-math', '-fvisibility=hidden',
              '-I' + str(root / 'loom/include'), '-I' + str(root / 'orbital/include'),
              '-I' + str(root / 'ikea/include')]
    cross = ['--target=x86_64-linux-gnu', '--sysroot=/usr/x86_64-linux-gnu',
             '-isystem', '/usr/x86_64-linux-gnu/include/c++/15', '-isystem',
             '/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu']
    targets = {'v2': ('arm', ['-mcpu=neoverse-v2']),
               'zen5-avx2': ('x86', cross + ['-march=znver5', '-mno-avx512f']),
               'zen5': ('x86', cross + ['-march=znver5'])}
    receipt = {'compiler': subprocess.check_output(['clang++-21', '--version'], text=True),
        'recipe_sha256': sha(Path(__file__)), 'source_root': str(root),
        'source_sha256': {str(p): sha(root / p) for p in sources},
        'capture_receipt': str(root.with_suffix('.capture.json')),
        'limits': ['Static instruction sites, not dynamic counts or cycles.',
                   'Object call targets use relocations, not zero displacements.',
                   'All functions have real separate-TU visibility; no LTO.',
                   'v2/Zen5 objects are compile-only on the local AArch64 VM.',
                   'Different installed libstdc++ headers are recorded, not assumed identical.'],
        'targets': {}, 'negative_controls': []}
    for name, (arch, flags) in targets.items():
        directory = output / name
        directory.mkdir()
        entries = []
        for relative in sources:
            label = str(relative).replace('/', '_').removesuffix('.cpp')
            obj = directory / (label + '.o')
            extra = ['-DCB_METADATA=4'] if relative == study / 'ring.cpp' and arch == 'x86' else []
            command = ['clang++-21', *common, *flags, *extra, '-c', str(root / relative), '-o', str(obj)]
            subprocess.run(command, check=True)
            report = inspect(obj, obj.with_suffix('.asm'), arch)
            entry = {'source': str(relative), 'object_sha256': sha(obj), 'argv': command,
                     'functions': report}
            if relative.name in ('returns.cpp', 'callers.cpp', 'worker.cpp'):
                ir = obj.with_suffix('.ll')
                ir_command = ['clang++-21', *common, *flags, '-S', '-emit-llvm',
                              str(root / relative), '-o', str(ir)]
                subprocess.run(ir_command, check=True)
                entry['ir_argv'] = ir_command
                entry['ir_signatures'] = [line for line in ir.read_text().splitlines()
                                          if line.startswith(('define ', 'declare '))]
            entries.append(entry)
            if relative == study / 'ring.cpp' and arch == 'x86':
                stress = obj.with_name('ring-metadata8.o')
                stress_command = ['clang++-21', *common, *flags, '-DCB_METADATA=8',
                                  '-c', str(root / relative), '-o', str(stress)]
                run = subprocess.run(stress_command, capture_output=True, text=True)
                stress.with_suffix('.log').write_text(run.stdout + run.stderr)
                control = {'target': name, 'argv': stress_command, 'returncode': run.returncode,
                           'diagnostic': run.stderr,
                           'qualification': 'Compile limit probe, not an expected portable capacity'}
                if run.returncode == 0:
                    control['functions'] = inspect(stress, stress.with_suffix('.asm'), arch)
                receipt['negative_controls'].append(control)
        receipt['targets'][name] = entries
        print(f'{name}: inspected {len(entries)} objects', flush=True)
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
