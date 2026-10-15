#!/usr/bin/env python3
"""Small compile-only investigation; this does not select production policy."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent
COMMON = ['clang++-21', '-std=c++23', '-O3', '-fno-vectorize',
          '-fno-slp-vectorize', '-fno-unroll-loops', '-fno-fast-math',
          '-ffp-contract=off', '--target=x86_64-linux-gnu']
TARGETS = {
    'zen5': ['-march=x86-64-v3', '-mtune=znver5'],
    'granite': ['-march=x86-64-v4', '-mtune=graniterapids'],
}
OPTIONS = {
    'baseline': [],
    'no_and_shrink': ['-mllvm', '-x86-and-imm-shrink=false'],
    'no_bw_fixup': ['-mllvm', '-fixup-byte-word-insts=false'],
    'clearance256': ['-mllvm', '-partial-reg-update-clearance=256'],
}
WIDTHS = [0, 1, 8, 16, 24, 32, 33, 40, 48, 56, 63, 64, 65, 256, '4294967295u']


def functions(assembly):
    result = {}
    for match in re.finditer(r'^([A-Za-z_][A-Za-z_0-9]*):[^\n]*.*?^\s*\.size\s+\1[^\n]*',
                             assembly, re.M | re.S):
        ins = [line.split('#')[0].strip() for line in match.group().splitlines()
               if line.startswith('\t') and not line.lstrip().startswith(('.', '#'))]
        result[match.group(1)] = {
            'movb': sum(i.startswith('movb') for i in ins),
            'movl': sum(i.startswith('movl') for i in ins),
            'bzhi': sum(i.startswith('bzhi') for i in ins),
            'bextr': sum(i.startswith('bextr') for i in ins),
            'push': sum(i.startswith('push') for i in ins),
            'call': sum(i.startswith('call') for i in ins),
            'instructions': ins,
        }
    return result


def main():
    version = subprocess.check_output(['clang++-21', '--version'], text=True).splitlines()[0]
    assert '21.1.8' in version, version
    report = {'compiler': version, 'scope': 'Compile-only, no x86 execution or timing.',
              'sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                          for name in ['count.cpp', 'run.py']}, 'targets': {}}
    for target, tuning in TARGETS.items():
        output = {}
        for name, extra in OPTIONS.items():
            command = COMMON + tuning + extra + ['-S', str(ROOT / 'count.cpp'),
                                                 '-o', str(ROOT / f'{target}-{name}.s')]
            subprocess.run(command, check=True, capture_output=True, text=True)
            assembly = (ROOT / f'{target}-{name}.s').read_text()
            bodies = functions(assembly)
            assert bodies['plain']['movb'] == bodies['plain']['bzhi'] == 1
            assert bodies['plain_producer']['bzhi'] == 1
            assert bodies['intrinsic_producer']['bzhi'] == 2
            assert bodies['intrinsic_producer']['movb'] == 1
            assert bodies['extract_producer']['bextr'] == 1
            assert bodies['extract_producer']['bzhi'] == bodies['extract_producer']['movb'] == 0
            assert bodies['extract_load']['bextr'] == 1
            for width in WIDTHS:
                body = bodies[f'extract_width_{width}']
                if 33 <= int(str(width).rstrip('u')) <= 63:
                    assert body['bextr'] == body['movl'] == 1
                    assert body['movb'] == body['bzhi'] == body['call'] == 0
                    assert f'${int(str(width).rstrip("u")) << 8},' in '\n'.join(body['instructions'])
                else:
                    assert body['bextr'] == body['call'] == 0
            output[name] = {'command': command,
                            'assembly_sha256': hashlib.sha256(assembly.encode()).hexdigest(),
                            'functions': bodies}
        baseline = output['baseline']['functions']
        for name in OPTIONS:
            for function in baseline:
                assert output[name]['functions'][function]['instructions'] == baseline[function]['instructions'], (target, name, function)
        # Independently ask the assembler to accept each compiled target body.
        object_command = COMMON + tuning + ['-c', str(ROOT / 'count.cpp'),
                                           '-o', str(ROOT / f'{target}.o')]
        subprocess.run(object_command, check=True, capture_output=True, text=True)
        report['targets'][target] = {'variants': output, 'object_command': object_command,
                                     'object_sha256': hashlib.sha256((ROOT / f'{target}.o').read_bytes()).hexdigest()}
        print(target, 'PASS: width, producer, memory-fold and unchanged-option checks')
    (ROOT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
