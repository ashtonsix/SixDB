#!/usr/bin/env python3
"""Actual admitted-write ABI, sizes and compiler output; no timings."""
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
    parser.add_argument('--candidate-root', type=Path, required=True)
    parser.add_argument('--baseline-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    args = parser.parse_args()
    source, baseline, output = (p.resolve() for p in
                                [args.candidate_root, args.baseline_root, args.output])
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output(['/usr/bin/clang++-21', '--version'], text=True).strip()
    if '21.1.8' not in version:
        raise RuntimeError('use pinned Clang 21.1.8')
    common = ['/usr/bin/clang++-21', '-std=c++23', '-O3', '-g0', '-UNDEBUG',
              '-Wall', '-Wextra', '-Wpedantic', '-fno-fast-math', '-ffp-contract=off',
              '-fdenormal-fp-math=ieee', '-DCONSUMER_METADATA_FROZEN=1',
              '-DSIXDB_TUNE_NEOVERSE_V3=0', '-I'+str(baseline),
              '-I'+str(baseline/'ikea/include'), '-I'+str(baseline/'orbital/include'),
              '-I'+str(source)]
    targets = {
        'aarch64': ['-march=armv8-a+simd', '-mtune=neoverse-v2'],
        'x86_64': ['--target=x86_64-linux-gnu',
                   '--gcc-install-dir=/usr/lib/gcc-cross/x86_64-linux-gnu/15',
                   '--sysroot=/', '-isystem', '/usr/x86_64-linux-gnu/include',
                   '-march=x86-64-v3', '-mtune=znver5'],
    }
    probe = output/'probes/write_batch_sizes.cpp'
    probe.parent.mkdir(exist_ok=True)
    shutil.copyfile(Path(__file__).resolve().parent/probe.name, probe)
    commands, facts = [], {}

    def run(argv, capture=False):
        argv = ['taskset', '-c', str(args.cpu), *argv]
        commands.append(shlex.join(argv))
        (output/'commands.txt').write_text('\n'.join(commands)+'\n')
        return (subprocess.check_output(argv, text=True) if capture else
                subprocess.run(argv, check=True))

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    input_hashes = {str(p.relative_to(baseline)): digest(p) for p in baseline.rglob('*')
                    if p.is_file() and p.suffix in ['.h', '.cpp']}
    candidate_hashes = {name: digest(source/name)
                        for name in ['write_batch.h', 'write_batch.cpp', 'write_batch_check.cpp']}
    for arch, flags in targets.items():
        dest = output/arch
        dest.mkdir(exist_ok=True)
        for suffix, extra in [('ll', ['-S', '-emit-llvm']), ('s', ['-S']), ('o', ['-c'])]:
            run([*common, *flags, str(source/'write_batch.cpp'), *extra,
                 '-o', str(dest/('write_batch.'+suffix))])
        ir = (dest/'write_batch.ll').read_text()
        if 'llvm.global_ctors' in ir:
            raise RuntimeError('unexpected global initialization')
        signatures = {}
        for line in ir.splitlines():
            if line.startswith('define ') and '11write_batch' in line:
                if '5applyE' in line or '15generic_controlE' in line:
                    signatures['apply' if '5applyE' in line else 'generic_control'] = {
                        'ordinary_return': 'indirect' if 'sret(' in line else 'register',
                        'llvm_signature': line,
                    }
        if set(signatures) != {'apply', 'generic_control'}:
            raise RuntimeError('missing actual ordinary endpoint signatures')
        run([*common, *flags, '-ffunction-sections', '-fdata-sections', str(probe),
             '-Wl,--gc-sections', '-o', str(dest/'sizes')])
        execution = [str(dest/'sizes')]
        if arch != platform.machine():
            execution = ['qemu-'+arch, '-L', '/usr/x86_64-linux-gnu', *execution]
        sizes = json.loads(run(execution, capture=True))
        (dest/'sizes.json').write_text(json.dumps(sizes, indent=2)+'\n')
        raw = run(['llvm-size-21', '-A', str(dest/'write_batch.o')], capture=True)
        (dest/'sections.txt').write_text(raw)
        sections = {match.group(1): int(match.group(2)) for line in raw.splitlines()
                    if (match := re.match(r'^(\S+)\s+(\d+)\s+\d+\s*$', line))}
        facts[arch] = {
            'sizes': sizes, 'signatures': signatures,
            'extra_global_initialization': False,
            'combined_object': {
                'text_bytes': sum(value for name, value in sections.items() if name.startswith('.text')),
                'rodata_bytes': sum(value for name, value in sections.items() if name.startswith('.rodata')),
                'sections': sections,
            },
        }
        print(arch, 'write ABI/sizes/code recorded', flush=True)
    if any(digest(baseline/p) != h for p, h in input_hashes.items()):
        raise RuntimeError('baseline sources changed during probe')
    if any(digest(source/p) != h for p, h in candidate_hashes.items()):
        raise RuntimeError('candidate sources changed during probe')
    receipt = {
        'scope': 'ordinary admitted-write ABI and compiler output, no timing',
        'compiler': version, 'cxx_library': 'libstdc++15 both targets', 'cpu': args.cpu,
        'baseline_commit': '7f73c81', 'baseline_root': str(baseline),
        'candidate_root': str(source), 'candidate_hashes': candidate_hashes,
        'baseline_hashes': input_hashes, 'facts': facts,
        'probe_hashes': {'write_batch_probe.py': digest(Path(__file__)), probe.name: digest(probe)},
        'limits': ['x86 size executable runs in qemu, never for timings',
                   'combined object includes both endpoints and their local callees, excluding external baseline/Ikea bodies',
                   'type sizes do not establish optimized frames or total path peaks',
                   'no owner, clone, publication, history, allocation or reclamation included'],
        'output_hashes': {str(p.relative_to(output)): digest(p) for p in output.rglob('*')
                          if p.is_file() and p.name != 'receipt.json'},
    }
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == '__main__':
    main()
