#!/usr/bin/env python3
"""Measure a pinned header closure and linked footprint; no throughput claims."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--boost', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path.cwd()
source = Path(__file__).with_name('probe.cpp').resolve()
boost = args.boost.resolve()
assert re.search(r'#define BOOST_VERSION\s+109200\b',
                 (boost / 'boost/version.hpp').read_text()), 'This audit pins Boost 1.92.0'
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
run = lambda cmd, **kw: subprocess.run(cmd, check=True, text=True, **kw)
report = {'compiler': run(['clang++-21', '--version'], capture_output=True).stdout,
          'boost_version': '1.92.0', 'probe_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'scope': 'build, independent lookup, existing modify, erase, iteration and copy; no growth/MVCC/staged operations',
          'cases': []}
(out / 'compiler.log').write_text('')
platforms = [
    ('neon', ['-march=armv8-a+simd']),
    ('avx512', ['--target=x86_64-linux-gnu', '-isystem', '/usr/x86_64-linux-gnu/include',
                '--gcc-toolchain=/usr', '-fuse-ld=lld', '-march=znver5', '-mtune=znver5']),
    ('avx2', ['--target=x86_64-linux-gnu', '-isystem', '/usr/x86_64-linux-gnu/include',
              '--gcc-toolchain=/usr', '-fuse-ld=lld', '-march=x86-64-v3', '-mtune=znver5'])]
closures = {}
for isa, target in platforms:
    common = ['clang++-21', '-std=c++23', '-O3', '-g0',
              '-ffunction-sections', '-fdata-sections', '-fvisibility=hidden',
              '-fvisibility-inlines-hidden', '-fno-fast-math', '-ffp-contract=off',
              '-I' + str(boost), '-I' + str(root / 'ikea/include'), *target]
    support = []
    for part in ['tuplepack/description', 'tuplepack/prepare', 'tuplepack/gpr', 'tuplepack/packet_layout',
                 'seriespack/admission', 'overlap']:
        obj = out / (isa + '-' + part.replace('/', '-') + '.o')
        support.append(str(obj))
        cmd = [*common, '-c', str(root / 'ikea/src' / (part + '.cpp')), '-o', str(obj)]
        run(cmd, stdout=subprocess.DEVNULL, stderr=(out / 'compiler.log').open('a'))
    for palette in [0, 1]:
        for impl, name in [(2, 'empty'), (0, 'boost'), (1, 'ikea')]:
            stem = out / f'{isa}-{palette}-{name}'
            cmd = [*common, f'-DADOPTION_IMPL={impl}', f'-DADOPTION_PALETTE={palette}',
                   '-MMD', '-MF', str(stem) + '.d', str(source),
                   *(support if impl == 1 else []),
                   '-Wl,--gc-sections', '-o', str(stem)]
            start = time.monotonic()
            run(['/usr/bin/time', '-v', '-o', str(stem) + '.time', *cmd],
                stdout=subprocess.DEVNULL, stderr=(out / 'compiler.log').open('a'))
            elapsed = time.monotonic() - start
            dep = Path(str(stem) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split()
            paths = sorted({Path(p).resolve() for p in dep})
            selected = [p for p in paths if p.is_relative_to(boost)]
            closures[(isa, name)] = selected
            modules = collections.Counter()
            for p in selected:
                rel = p.relative_to(boost / 'boost')
                modules[rel.parts[0]] += len(p.read_bytes().splitlines())
            sections = {}
            for line in run(['llvm-size-21', '-A', str(stem)], capture_output=True).stdout.splitlines():
                fields = line.split()
                if len(fields) >= 2 and fields[0].startswith('.'):
                    sections[fields[0]] = int(fields[1])
            stripped = str(stem) + '.stripped'
            run(['llvm-strip-21', '--strip-all', '-o', stripped, str(stem)])
            sample = dict(isa=isa, palette=palette, implementation=name, command=cmd,
                          compile_link_seconds=elapsed,
                          compile_peak_kib=int(re.search(r'Maximum resident set size \(kbytes\): (\d+)',
                              Path(str(stem) + '.time').read_text())[1]),
                          text=sections.get('.text', 0), rodata=sections.get('.rodata', 0),
                          sections=sections, stripped_bytes=Path(stripped).stat().st_size,
                          boost_headers=len(selected), boost_bytes=sum(p.stat().st_size for p in selected),
                          boost_physical_lines=sum(len(p.read_bytes().splitlines()) for p in selected),
                          boost_lines_by_prefix=dict(sorted(modules.items())))
            if isa == 'neon':
                sample['checksums'] = [run([str(stem), str(n), str(seed)], capture_output=True).stdout.strip()
                                       for n, seed in [(1, 713), (1024, 991), (8192, 1729)]]
            report['cases'].append(sample)
            print(isa, palette, name, sample['text'], sample['stripped_bytes'], flush=True)
            (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
for palette in [0, 1]:
    real = [r['checksums'] for r in report['cases']
            if r['isa'] == 'neon' and r['palette'] == palette and r['implementation'] != 'empty']
    assert len(real) == 2 and real[0] == real[1], (palette, real)
    def expected(w, v, n, seed):
        # Independent scalar oracle for the probe's externally visible work.
        removed = n // 4
        return (n + (sum(i % 256 for i in range(n)) if v else 0) + removed +
                sum((((i * 997) ^ seed) & ((1 << w) - 1)) +
                    ((i + 1) % 256 if v else 0) for i in range(removed, n)) + n - removed)
    types = [(40, 8)] + ([(24, 0), (24, 8), (40, 0), (40, 64)] if palette else [])
    oracle = [str(sum(expected(w, v, n, seed) for w, v in types))
              for n, seed in [(1, 713), (1024, 991), (8192, 1729)]]
    assert real[0] == oracle, (palette, real[0], oracle)
paths = sorted(set().union(*(set(v) for (isa, name), v in closures.items() if name == 'boost')))
(out / 'boost-headers.json').write_text(json.dumps([
    {'path': str(p.relative_to(boost)), 'lines': len(p.read_bytes().splitlines()),
     'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
    for p in paths], indent=2) + '\n')
print('Native checksums match for both representations; cross binaries were not executed.', flush=True)
