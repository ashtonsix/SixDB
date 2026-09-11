#!/usr/bin/env python3
"""Relink retained Zen K10 objects at four placements; never compile inputs."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess

SYMBOL = '_ZN4ikea10seriespack6detail12_GLOBAL__N_112decode_boundINS2_10avx512_opsELj2ELNS0_8geometryE0ELj8EEEvRKNS0_10basic_viewIKSt4byteEENS0_11index_rangeENS0_13output_valuesE'
EXPECTED_BINARY = 'f6f04c70b27ed97d322c8a03399adbc138c49c1d5844b2ee04bf785813a1cb10'
EXPECTED_LIBRARY = 'c269f5c148f9e3107c3ae53a2289a6691d2c2740c5103a2b3b717a616a76d259'
CASES = {f'bulk/series/{isa}/local/k10/h8/u16/decode' for isa in ('avx2', 'avx512')}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def capture(argv, cwd=None):
    return subprocess.check_output(argv, cwd=cwd, text=True)


def elf_bytes(path, address, size):
    data = path.read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01', 'requires little-endian ELF64'
    phoff = struct.unpack_from('<Q', data, 32)[0]
    entsize, count = struct.unpack_from('<HH', data, 54)
    for i in range(count):
        typ, flags, offset, vaddr, paddr, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * entsize)
        if typ == 1 and vaddr <= address and address + size <= vaddr + filesz:
            return data[offset + address - vaddr:offset + address - vaddr + size]
    raise ValueError('constant not in ELF load segment')


def inspect(binary, output, label):
    nm = capture(['llvm-nm-21', '-S', '--defined-only', str(binary)])
    matches = [line.split() for line in nm.splitlines() if line.endswith(' ' + SYMBOL)]
    assert len(matches) == 1
    address, size = (int(n, 16) for n in matches[0][:2])
    asm = capture(['llvm-objdump-21', '-d', '--no-show-raw-insn', '--disassemble-symbols=' + SYMBOL, str(binary)])
    (output / (label + '.asm')).write_text(asm)
    normalized, constants = [], []
    for line in asm.splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+(.*)', line)
        if not match:
            continue
        if not address <= int(match[1], 16) < address + size:
            continue  # objdump may include alignment padding after this symbol.
        instruction = match[2]
        if '(%rip)' in instruction:
            reference = re.search(r'#\s*0x([0-9a-f]+)', instruction)
            assert reference, instruction
            # This exact decoder loads 128-bit shuffle masks and 64-bit broadcasts.
            length = 8 if instruction.startswith('vpbroadcastq') else 16
            constants.append(elf_bytes(binary, int(reference[1], 16), length).hex())
            instruction = re.sub(r'-?0x[0-9a-f]+\(%rip\)', 'CONST(%rip)', instruction)
        instruction = instruction.split('#')[0].strip()
        def target(match):
            destination = int(match[1], 16)
            if address <= destination < address + size:
                return f'<decoder+0x{destination - address:x}>'
            return match[2]
        instruction = re.sub(r'0x([0-9a-f]+)\s+(<[^>]+>)', target, instruction)
        normalized.append((int(match[1], 16) - address, instruction))
    assert normalized and constants
    return {'address': hex(address), 'address_mod64': address % 64,
            'u16_loop_address': hex(address + 0x3b0), 'u16_loop_mod64': (address + 0x3b0) % 64,
            'size': size, 'instructions': normalized, 'constants': constants}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('/opt/sixdb/source/build/validation/seriespack/zen5/avx512/Release'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve()
    output = args.output or Path(os.environ['SIXDB_RESULTS']) / 'placement'
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = build / 'workbench/spikes/ikea-composition/validation/ikea_seriespack_bench'
    assert digest(binary) == EXPECTED_BINARY, 'retained candidate binary changed'
    lines = capture(['ninja', '-C', str(build), '-t', 'commands', 'ikea_seriespack_bench']).splitlines()
    tokens = shlex.split(lines[-1])
    # CMake wraps a single linker invocation in optional ': && ... && :'.
    if tokens[:2] == [':', '&&']:
        tokens = tokens[2:]
    if tokens[-2:] == ['&&', ':']:
        tokens = tokens[:-2]
    assert not any(t in {'&&', ';', '|', '||'} or t.startswith('@') for t in tokens)
    assert '-o' in tokens and '-c' not in tokens
    inputs = {str((build / t).resolve()): digest((build / t).resolve())
              for t in tokens if t.endswith(('.o', '.a'))}
    assert EXPECTED_LIBRARY in inputs.values(), 'candidate SeriesPack archive changed'
    baseline = output / 'baseline'
    shutil.copy2(binary, baseline)
    receipt = {'scope': 'linker-only diagnostic; original objects; fixture oracles, no new aggregate build',
               'script_sha256': digest(Path(__file__)), 'original_link_command': lines[-1],
               'inputs_sha256': inputs, 'variants': {}, 'trials': [], 'status': 'running'}
    receipt_path = output / 'placement.json'
    save(receipt_path, receipt)
    try:
        reference = inspect(baseline, output, 'baseline')
        receipt['variants']['baseline'] = {'sha256': digest(baseline), **reference}
        for pad in (0, 16, 32, 48):
            label = f'pad{pad}'
            script = output / (label + '.ld')
            script.write_text('SECTIONS { .seriespack_k10 ALIGN(64) : {\n'
                              '  __seriespack_probe_start = .;\n'
                              f'  . += {pad}; KEEP(*(.text.{SYMBOL}))\n'
                              '  . = __seriespack_probe_start + 4096;\n'
                              '} } INSERT BEFORE .text;\n')
            argv = list(tokens)
            argv[argv.index('-o') + 1] = str(output / label)
            argv += ['-Wl,-T,' + str(script), '-Wl,-Map,' + str(output / (label + '.map'))]
            with (output / (label + '.link.txt')).open('w') as log:
                subprocess.run(argv, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
            info = inspect(output / label, output, label)
            assert info['instructions'] == reference['instructions'], f'{label}: decoder instructions changed'
            assert info['constants'] == reference['constants'], f'{label}: decoder constants changed'
            assert info['address_mod64'] == pad
            receipt['variants'][label] = {'argv': argv, 'sha256': digest(output / label), **info}
            save(receipt_path, receipt)
        assert inputs == {p: digest(Path(p)) for p in inputs}, 'link inputs changed'
        assert digest(binary) == EXPECTED_BINARY, 'original binary changed'
        if not args.prepare_only:
            cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
            assert cpu in os.sched_getaffinity(0)
            receipt['cpu'] = cpu
            environment = os.environ | {'SERIESPACK_BULK_VALUES': '8192'}
            for i, label in enumerate(('baseline', 'pad0', 'pad16', 'pad32', 'pad48', 'baseline'), 1):
                name = f'{i:02d}-{label}'
                samples = output / (name + '.json')
                argv = ['taskset', '-c', str(cpu), str(output / label),
                        '--benchmark_filter=^bulk/series/(avx2|avx512)/local/k10/h8/u16/decode$',
                        '--benchmark_min_time=0.2s', '--benchmark_repetitions=5',
                        '--benchmark_enable_random_interleaving=false',
                        '--benchmark_out_format=json', '--benchmark_out=' + str(samples)]
                with (output / (name + '.txt')).open('w') as log:
                    subprocess.run(argv, cwd=build, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
                rows = json.loads(samples.read_text())['benchmarks']
                raw = [r for r in rows if r.get('run_type') == 'iteration']
                assert Counter(r['run_name'] for r in raw) == {case: 5 for case in CASES}
                assert not any(r.get('error_occurred') for r in rows)
                assert all(r['iterations'] > 0 and math.isfinite(r['cpu_time']) for r in raw)
                receipt['trials'].append({'name': name, 'argv': argv, 'samples_sha256': digest(samples)})
                save(receipt_path, receipt)
        receipt['status'] = 'prepared' if args.prepare_only else 'complete'
    except Exception as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        save(receipt_path, receipt)


if __name__ == '__main__':
    main()
