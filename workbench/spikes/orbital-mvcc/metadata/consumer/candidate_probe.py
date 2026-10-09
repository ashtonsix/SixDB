#!/usr/bin/env python3
"""Ordinary candidate ABI, actual sizes and emitted code; no timing."""
import argparse
import hashlib
import json
import platform
import re
import shlex
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate-root', type=Path, required=True,
                        help='frozen metadata/consumer directory')
    parser.add_argument('--baseline-root', type=Path, required=True,
                        help='7f73c81 source archive with original include layout')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    args = parser.parse_args()
    source = args.candidate_root.resolve()
    baseline = args.baseline_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = '/usr/bin/clang++-21'
    version = subprocess.check_output([compiler, '--version'], text=True).strip()
    if '21.1.8' not in version:
        raise RuntimeError('use pinned Clang 21.1.8')
    common = [compiler, '-std=c++23', '-O3', '-g0', '-UNDEBUG', '-Wall', '-Wextra',
              '-Wpedantic', '-Wno-return-type-c-linkage', '-fno-fast-math',
              '-ffp-contract=off', '-fdenormal-fp-math=ieee',
              '-DCONSUMER_METADATA_FROZEN=1', '-DSIXDB_TUNE_NEOVERSE_V3=0',
              '-I'+str(baseline), '-I'+str(baseline/'ikea/include'),
              '-I'+str(baseline/'orbital/include'), '-I'+str(source)]
    targets = {
        'aarch64': ['-march=armv8-a+simd', '-mtune=neoverse-v2'],
        'x86_64': ['--target=x86_64-linux-gnu',
                   '--gcc-install-dir=/usr/lib/gcc-cross/x86_64-linux-gnu/15',
                   '--sysroot=/', '-isystem', '/usr/x86_64-linux-gnu/include',
                   '-march=x86-64-v3', '-mtune=znver5'],
    }
    # The hot-only probe is machinery added after the candidate source capture.
    # Copy it away from live headers; its quoted includes resolve to frozen ones.
    probe = output/'probes/native_law_abi.cpp'
    probe.parent.mkdir(exist_ok=True)
    shutil.copyfile(Path(__file__).resolve().parent/'native_law_abi.cpp', probe)
    commands, facts = [], {}

    def run(argv, capture=False):
        argv = ['taskset', '-c', str(args.cpu), *argv]
        commands.append(shlex.join(argv))
        (output/'commands.txt').write_text('\n'.join(commands)+'\n')
        return subprocess.check_output(argv, text=True) if capture else subprocess.run(argv, check=True)

    for arch, flags in targets.items():
        dest = output/arch
        dest.mkdir(exist_ok=True)
        signatures = {}
        for unit in ['candidate_abi', 'compact_abi']:
            for suffix, extra in [('ll', ['-S', '-emit-llvm']), ('s', ['-S']), ('o', ['-c'])]:
                run([*common, *flags, str(source/(unit+'.cpp')), *extra,
                     '-o', str(dest/(unit+'.'+suffix))])
            ir = (dest/(unit+'.ll')).read_text()
            if 'llvm.global_ctors' in ir:
                raise RuntimeError('unexpected global initialization in '+unit)
            for line in ir.splitlines():
                if line.startswith('define ') and '@consumer_' in line:
                    name = re.search(r'@(consumer_\w+)\(', line).group(1)
                    signatures[name] = {'ordinary_return': 'indirect' if 'sret(' in line else
                                       'void' if re.search(r'\bvoid @', line) else 'register',
                                       'llvm_signature': line}
        run([*common, *flags, str(source/'compact_sizes.cpp'), '-o', str(dest/'sizes')])
        execution = [str(dest/'sizes')]
        if arch != platform.machine():
            execution = ['qemu-'+arch, '-L', '/usr/x86_64-linux-gnu', *execution]
        sizes = json.loads(run(execution, capture=True))
        sections = {}
        for law, kind in [('schema', 1), ('compact', 2), ('canonical', 3)]:
            for suffix, extra in [('ll', ['-S', '-emit-llvm']), ('s', ['-S']), ('o', ['-c'])]:
                run([*common, *flags, '-DCONSUMER_LAW_PROBE='+str(kind), str(probe), *extra,
                     '-o', str(dest/('hot-'+law+'.'+suffix))])
            raw = run(['llvm-size-21', '-A', str(dest/('hot-'+law+'.o'))], capture=True)
            (dest/('hot-'+law+'-sections.txt')).write_text(raw)
            measured = {match.group(1): int(match.group(2)) for line in raw.splitlines()
                        if (match := re.match(r'^(\S+)\s+(\d+)\s+\d+\s*$', line))}
            sections[law] = {'text_bytes': sum(value for name, value in measured.items()
                                              if name.startswith('.text')),
                             'rodata_bytes': sum(value for name, value in measured.items()
                                                if name.startswith('.rodata')),
                             'sections': measured}
        facts[arch] = {'sizes': sizes, 'signatures': signatures, 'hot_only_object_sections': sections,
                       'extra_default_schema_global_initialization': False}
        print(arch, 'candidate ABI/sizes/code recorded', flush=True)

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    dependencies = [p for p in source.parent.rglob('*') if p.is_file() and p.suffix in ['.h', '.cpp']]
    receipt = {
        'scope': 'candidate ordinary ABI and compiler code; no timing',
        'compiler': version, 'cxx_library': 'libstdc++15 both targets', 'cpu': args.cpu,
        'baseline_commit': '7f73c81', 'baseline_root': str(baseline), 'candidate_root': str(source),
        'facts': facts,
        'limits': ['x86 sizeof execution uses qemu, never performance',
                   'hot-only object section sizes exclude cold qualification and linked deduplication',
                   'record sizes are not whole caller frames; ordinary error/owner ABI is separately probed'],
        'candidate_hashes': {str(p.relative_to(source.parent)): digest(p) for p in dependencies},
        'baseline_hashes': {str(p.relative_to(baseline)): digest(p) for p in baseline.rglob('*')
                            if p.is_file() and p.suffix in ['.h', '.cpp']},
        'probe_hashes': {'candidate_probe.py': digest(Path(__file__).resolve()),
                         'native_law_abi.cpp': digest(probe)},
        'output_hashes': {str(p.relative_to(output)): digest(p) for p in output.rglob('*')
                          if p.is_file() and p.name != 'receipt.json'},
    }
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == '__main__':
    main()
