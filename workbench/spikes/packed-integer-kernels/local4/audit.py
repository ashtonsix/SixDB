#!/usr/bin/env python3
"""Retain exact emitted loops and object text/constants; instruction counts are not timings."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def audit(binary):
    asm = subprocess.check_output(['llvm-objdump-21','--demangle','-d','--no-show-raw-insn',str(binary)],text=True)
    binary.with_suffix(binary.suffix+'.asm').write_text(asm)
    symbols = subprocess.check_output(['llvm-nm-21','-S','--demangle',str(binary)],text=True)
    sizes = {m[2]:int(m[1],16) for m in re.finditer(r'^[0-9a-f]+ ([0-9a-f]+) [tTwW] (.+)$',symbols,re.M)}
    result = {}
    for match in re.finditer(r'^([0-9a-f]+) <([^\n]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',asm,re.M|re.S):
        name = match[2]
        if not ('operation<' in name or 'predecessor(' in name or name.startswith('local4_u')):
            continue
        address,size = int(match[1],16),sizes[name]
        rows = []
        for line in match[3].splitlines():
            parsed = re.match(r'\s*([0-9a-f]+):\s+(.+)',line)
            if parsed and int(parsed[1],16)<address+size:
                rows.append((int(parsed[1],16),parsed[2].split('#',1)[0].strip()))
        loops = []
        for pc,ins in rows:
            branch = re.match(r'j[a-z]+\s+0x([0-9a-f]+)',ins)
            if branch and address<=int(branch[1],16)<pc:
                start = int(branch[1],16)
                instructions = [i for p,i in rows if start<=p<=pc]
                loops.append({'start_offset':start-address,'start_mod64':start%64,
                    'backward_branch_offset':pc-address,'instruction_count':len(instructions),
                    'vector_registers':sorted({int(n) for i in instructions for n in re.findall(r'%[xyz]mm([0-9]+)',i)}),
                    'stack_references':sum('%rsp' in i or '%rbp' in i for i in instructions),
                    'calls':sum(i.startswith('call') for i in instructions),'assembly':instructions})
        result[name] = {'entry_mod64':address%64,'size_bytes':size,'backward_loops':loops,
            'all_function_calls':[i for _,i in rows if i.startswith('call')]}
    assert result,'no diagnostic functions found'
    sections = subprocess.check_output(['llvm-size-21','--format=sysv',str(binary)],text=True)
    binary.with_suffix(binary.suffix+'.size.txt').write_text(sections)
    section_sizes = {m[1]:int(m[2]) for m in re.finditer(r'^(\.[\w.]+)\s+(\d+)\s+\d+\s*$',sections,re.M)}
    constants = subprocess.check_output(['llvm-objdump-21','-s',str(binary)],text=True)
    constant_sections = {}
    for m in re.finditer(r'^Contents of section (\.rodata[^:]*):\n(.*?)(?=^Contents of section |\Z)',constants,re.M|re.S):
        data = bytearray()
        for line in m[2].splitlines():
            parts = line.split()
            for word in parts[1:5]:
                if not re.fullmatch('[0-9a-f]{2,8}',word) or len(word)%2:
                    break
                data.extend(bytes.fromhex(word))
        constant_sections[m[1]] = {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
            'hex':data.hex() if len(data)<=256 else None}
    return {'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'section_sizes':section_sizes,'constant_sections':constant_sections,
        'interpretation':'Backward loops and static instruction counts are retained for inspection. Only actual paired hardware timing can establish benefit. Tables are per object/binary, not assigned to individual functions.',
        'functions':result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('output',type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(audit(args.binary),indent=2)+'\n')
    print(args.output)


if __name__ == '__main__':
    main()
