#!/usr/bin/env python3
"""Record exact function sizes and first backward-loop instructions for head arms."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def audit(binary):
    asm = subprocess.check_output(['llvm-objdump-21', '--demangle', '-d', '--no-show-raw-insn', str(binary)], text=True)
    binary.with_suffix('.asm').write_text(asm)
    symbols = subprocess.check_output(['llvm-nm-21', '-S', '--demangle', str(binary)], text=True)
    sizes = {m[2]: int(m[1], 16) for m in re.finditer(r'^[0-9a-f]+ ([0-9a-f]+) [tTwW] (.+)$', symbols, re.M)}
    result = {}
    for m in re.finditer(r'^([0-9a-f]+) <([^\n]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)', asm, re.M | re.S):
        name = m[2]
        if 'operation<head_encode_experiment::' not in name or ', unsigned long, ' not in name or ', false>' not in name:
            continue
        if not re.search(r', (1|7|40)u, 16u, (8|256)u,', name):
            continue
        address, size = int(m[1], 16), sizes[name]
        rows = []
        for line in m[3].splitlines():
            parsed = re.match(r'\s*([0-9a-f]+):\s+(.+)', line)
            if parsed and int(parsed[1], 16) < address + size:
                rows.append((int(parsed[1], 16), parsed[2].split('#', 1)[0].strip()))
        first_loop = None
        for pc, ins in rows:
            branch = re.match(r'j[a-z]+\s+0x([0-9a-f]+)', ins)
            if branch and address <= int(branch[1], 16) < pc:
                start = int(branch[1], 16)
                instructions = [i for p, i in rows if start <= p <= pc]
                regs = {int(n) for i in instructions for n in re.findall(r'%[xyz]mm([0-9]+)', i)}
                first_loop = {'start_offset': start-address, 'start_mod64': start%64,
                              'backward_branch_offset': pc-address, 'instructions': len(instructions),
                              'vector_registers': sorted(regs),
                              'stack_references': sum('(%rsp)' in i or '(%rbp)' in i for i in instructions),
                              'calls': sum(i.startswith('call') for i in instructions),
                              'memory_narrowing_stores': sum(bool(re.match(r'vpmov[qdw][bdw]\s+.*\(', i)) for i in instructions),
                              'assembly': instructions}
                break
        result[name] = {'address_mod64': address%64, 'size': size,
                        'first_backward_loop': first_loop,
                        'all_function_calls': [i for _,i in rows if i.startswith('call')]}
    assert result, 'no matching head operations found'
    return {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'interpretation': 'First backward loop is recorded for inspection, not an inferred CPU cost model. Boundary code and whole-operation payload calls are separate.',
            'functions': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(audit(args.binary), indent=2)+'\n')
    print(args.output)


if __name__ == '__main__':
    main()
