#!/usr/bin/env python3
"""Actual ordinary-ABI census; no performance measurement or source mutation."""
import argparse
import csv
import hashlib
import json
import platform
import re
import shlex
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--frozen', action='store_true', help='original 7f73c81 include layout')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    parser.add_argument('--compiler', default='/usr/bin/clang++-21')
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    captured = args.source_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([args.compiler, '--version'], text=True)
    if '21.1.8' not in version:
        raise RuntimeError('use the pinned Clang 21.1.8 compiler')
    baseline_include = captured / ('orbital/include' if args.frozen else 'baseline/include')
    common = [args.compiler, '-std=c++23', '-O3', '-g0', '-Wall', '-Wextra', '-Wpedantic',
              '-Wno-return-type-c-linkage', f'-DCONSUMER_METADATA_FROZEN={int(args.frozen)}',
              '-DSIXDB_TUNE_NEOVERSE_V3=0', '-I'+str(captured),
              '-I'+str(captured/'ikea/include'), '-I'+str(baseline_include)]
    # The relocated spike source root and module include root are independent.
    if not args.frozen:
        common += ['-I'+str(captured.parents[2]/'ikea/include'),
                   '-I'+str(captured.parents[2]/'orbital/include')]
    gcc = Path('/usr/lib/gcc-cross/x86_64-linux-gnu/15')
    if not gcc.exists():
        raise RuntimeError('x86-64 GCC15 cross headers/libraries are required')
    targets = {
        'aarch64': ['--target=aarch64-linux-gnu', '-march=armv8-a+simd', '-mtune=neoverse-v2'],
        'x86_64': ['--target=x86_64-linux-gnu', '--gcc-install-dir='+str(gcc), '--sysroot=/',
                   '-isystem', '/usr/x86_64-linux-gnu/include',
                   '-march=x86-64-v3', '-mtune=znver5'],
    }
    commands = []
    facts = {}

    def run(argv, *, capture=False):
        command = ['taskset', '-c', str(args.cpu), *argv]
        commands.append(shlex.join(command))
        return subprocess.check_output(command, text=True) if capture else subprocess.run(command, check=True)

    for arch, flags in targets.items():
        dest = output/arch
        dest.mkdir(exist_ok=True)
        run([*common, *flags, str(source/'census.cpp'), '-o', str(dest/'census')])
        signatures = {}
        locals_facts = []
        for unit in ['abi', 'locals_data', 'locals_consumer']:
            for suffix, extra in [('ll', ['-S', '-emit-llvm']), ('s', ['-S'])]:
                run([*common, *flags, str(source/(unit+'.cpp')), *extra,
                     '-o', str(dest/(unit+'.'+suffix))])
            ir = (dest/(unit+'.ll')).read_text()
            for line in ir.splitlines():
                if line.startswith('define ') and '@consumer_abi_' in line:
                    name = re.search(r'@consumer_abi_(\w+)\(', line).group(1)
                    signatures[name] = line
                if line.startswith('@consumer_local_'):
                    match = re.search(r'@consumer_local_(\w+) = .*constant \[6 x i64\] \[(.*?)\]', line)
                    values = [int(v) for v in re.findall(r'i64 (\d+)', match.group(2))]
                    keys = ['bytes', 'alignment', 'trivially_copyable', 'trivial_destructor',
                            'trivial_move', 'standard_layout']
                    locals_facts.append({'type': match.group(1), **{
                        key: value if index < 2 else bool(value)
                        for index, (key, value) in enumerate(zip(keys, values))}})
        execution = [str(dest/'census')]
        if arch != platform.machine():
            execution = ['qemu-'+arch, '-L', '/usr/x86_64-linux-gnu' if arch=='x86_64' else '/', *execution]
        rows = json.loads(run(execution, capture=True)) + locals_facts
        for row in rows:
            signature = signatures[row['type']]
            row['ordinary_return'] = 'indirect' if 'sret(' in signature else 'register'
            row['llvm_signature'] = signature
        facts[arch] = rows
        (dest/'packets.json').write_text(json.dumps(rows, indent=2)+'\n')
        print(arch, len(rows), 'actual packets:', sum(r['ordinary_return']=='register' for r in rows),
              'ordinary register returns')
    (output/'commands.txt').write_text('\n'.join(commands)+'\n')
    a = {r['type']: r for r in facts['aarch64']}
    x = {r['type']: r for r in facts['x86_64']}
    assert a.keys() == x.keys()
    with (output/'packets.csv').open('w') as file:
        writer = csv.writer(file)
        writer.writerow(['type', 'aarch64_bytes', 'x86_64_bytes', 'aarch64_align', 'x86_64_align',
                         'trivial_destructor', 'trivial_move', 'trivially_copyable',
                         'aarch64_ordinary_return', 'x86_64_ordinary_return'])
        for name, row in a.items():
            writer.writerow([name, row['bytes'], x[name]['bytes'], row['alignment'], x[name]['alignment'],
                             row['trivial_destructor'], row['trivial_move'], row['trivially_copyable'],
                             row['ordinary_return'], x[name]['ordinary_return']])
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    probe_paths = [source/p for p in ['types.h','census.cpp','abi.cpp','locals_data.cpp','locals_consumer.cpp','probe.py']]
    frozen_paths = [p for p in captured.rglob('*') if p.is_file() and p.suffix in ['.h','.cpp']]
    receipt = {'scope':'metadata sizes and ordinary ABI; no timing', 'baseline_commit':'7f73c81' if args.frozen else None,
               'compiler':version.strip(), 'cxx_library':'libstdc++15 on both targets', 'cpu':args.cpu,
               'x86_execution':'qemu size-report execution; not a performance measurement',
               'source_root':str(captured), 'commands':'commands.txt',
               'probe_hashes':{str(p.relative_to(source)):digest(p) for p in probe_paths},
               'baseline_hashes':{str(p.relative_to(captured)):digest(p) for p in frozen_paths},
               'output_hashes':{str(p.relative_to(output)):digest(p) for p in output.rglob('*')
                                if p.is_file() and p.name != 'receipt.json' and
                                p.suffix in ['.ll','.s','.csv','.json']}}
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == '__main__':
    main()
