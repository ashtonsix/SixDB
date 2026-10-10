#!/usr/bin/env python3
"""Build one identical native caller against two immutable Schema projections.

Build only unless --check or --benchmark is explicitly given. No source projection is edited.
"""
import argparse
import hashlib
import json
import platform
import shlex
import shutil
import subprocess
from pathlib import Path


CODE_SUFFIXES = {'.h', '.hpp', '.cpp', '.cc', '.inc', '.cmake', '.txt', '.json'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {str(p.relative_to(root)): digest(p) for p in sorted(root.rglob('*'))
            if p.is_file() and p.suffix in CODE_SUFFIXES}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-root', type=Path, required=True)
    parser.add_argument('--candidate-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--codegen', action='store_true', help='optionally emit data/probe LLVM and data assembly')
    execution = parser.add_mutually_exclusive_group()
    execution.add_argument('--check', action='store_true', help='execute sizes and behavior without clocks')
    execution.add_argument('--benchmark', action='store_true', help='execute bounded clocks in an authorized worker slot')
    args = parser.parse_args()
    machine = platform.machine()
    if platform.system() != 'Linux' or machine not in {'aarch64', 'arm64', 'x86_64', 'AMD64'}:
        raise RuntimeError('run natively in pinned AArch64 or x86_64 Linux')
    arm = machine in {'aarch64', 'arm64'}
    march, tune, compiler_tune = ('armv8-a+simd', 'neoverse-v2', 'neoverse-v2') if arm else ('x86-64-v3', 'zen5', 'znver5')
    if args.jobs < 1 or args.cpu < 0:
        raise RuntimeError('jobs must be positive and CPU nonnegative')
    roots = {'base': args.base_root.resolve(), 'candidate': args.candidate_root.resolve()}
    output = args.output.resolve()
    if roots['base'] == roots['candidate']:
        raise RuntimeError('base and candidate must be distinct archived projections')
    for label, root in roots.items():
        if output == root or root in output.parents:
            raise RuntimeError('output must be outside source projections')
        if not (root/'workbench/spikes/orbital-mvcc/consumer/data.cpp').is_file():
            raise RuntimeError('projection lacks actual data.cpp: '+label)
    machinery = Path(__file__).resolve().parent
    before = {label: inventory(root) for label, root in roots.items()}
    probe_hash = digest(machinery/'schema_phase_probe.cpp')
    output.mkdir(parents=True, exist_ok=True)
    probes = output/'probes'
    probes.mkdir(exist_ok=True)
    for name in ['schema_phase_probe.cpp', 'schema_phase_probe.py', 'schema_phase_notes.md']:
        shutil.copyfile(machinery/name, probes/name)
    commands = []

    def run(argv, capture=False):
        command = ['taskset', '-c', str(args.cpu), *map(str, argv)]
        commands.append(shlex.join(command))
        (output/'commands.txt').write_text('\n'.join(commands)+'\n')
        return (subprocess.check_output(command, text=True) if capture else
                subprocess.run(command, check=True))

    compiler = '/usr/bin/clang++-21'
    version = run([compiler, '--version'], capture=True).strip()
    if '21.1.8' not in version:
        raise RuntimeError('use pinned Clang 21.1.8')
    facts = {'compiler': version, 'cpu': args.cpu, 'native_machine': machine, 'march': march, 'tune': tune,
             'codegen': args.codegen, 'base_tree': 'e509697457a4a61bee8adcd2c5631bd40ee9f221',
             'probe_sha256': probe_hash, 'source_before': before,
             'execution': 'benchmark' if args.benchmark else 'check' if args.check else 'build_only',
             'scope': 'actual ordinary native data calls; allocation and admission outside clocks; no Store/publication'}
    for label, root in roots.items():
        dest = output/label
        build = dest/'build'
        dest.mkdir(exist_ok=True)
        run(['cmake', '-S', root, '-B', build, '-G', 'Ninja',
             '-DCMAKE_CXX_COMPILER='+compiler, '-DCMAKE_BUILD_TYPE=Release',
             '-DSIXDB_SPIKES=orbital-mvcc', '-DSIXDB_BENCHMARKS=',
             '-DSIXDB_MARCH='+march, '-DSIXDB_TUNE='+tune])
        run(['cmake', '--build', build, '--target', 'orbital_versioned_data', '-j'+str(args.jobs)])
        include = [root, root/'ikea/include', root/'orbital/include',
                   root/'workbench/spikes/orbital-mvcc/baseline/include',
                   root/'workbench/benchmarks/orbital']
        flags = [compiler, '-std=c++23', '-O3', '-g0', '-UNDEBUG',
                 '-Wall', '-Wextra', '-Wpedantic', '-fno-fast-math', '-ffp-contract=off',
                 '-fdenormal-fp-math=ieee', '-march='+march, '-mtune='+compiler_tune,
                 '-DSIXDB_TUNE_NEOVERSE_V3=0', '-DSIXDB_TUNE_NEOVERSE_V2='+str(int(arm)),
                 '-DSIXDB_TUNE_ZEN5='+str(int(not arm)), *['-I'+str(path) for path in include]]
        probe = probes/'schema_phase_probe.cpp'
        binary = dest/'schema_phase_probe'
        libraries = [build/'workbench/spikes/orbital-mvcc/consumer/liborbital_versioned_data.a',
                     build/'ikea/libikea_tuplepack.a', build/'ikea/libikea_seriespack.a',
                     build/'ikea/libikea_core.a', build/'orbital/liborbital.a']
        if not all(path.is_file() for path in libraries):
            raise RuntimeError('expected ordinary CMake static archives are missing for '+label)
        run([*flags, probe, '-MMD', '-MF', dest/'probe.d', *libraries, '-pthread', '-o', binary])
        if args.codegen:
            run([*flags, probe, '-S', '-emit-llvm', '-o', dest/'probe.ll'])
            run([*flags, root/'workbench/spikes/orbital-mvcc/consumer/data.cpp',
                 '-S', '-emit-llvm', '-o', dest/'data.ll'])
            run([*flags, root/'workbench/spikes/orbital-mvcc/consumer/data.cpp', '-S', '-o', dest/'data.s'])
        facts[label] = {'source_root': str(root), 'binary_sha256': digest(binary),
                        'archives_sha256': {str(p.relative_to(build)): digest(p) for p in libraries}}

    # A bounded AB/BA order, with all three repetitions retained in each report.
    # This is a local diagnostic, not a hardware uplift or tail-latency campaign.
    if args.check or args.benchmark:
        for label in ['base', 'candidate']:
            raw = run([output/label/'schema_phase_probe', '--sizes-only'], capture=True)
            sizes = json.loads(raw)
            (output/label/'sizes.json').write_text(json.dumps(sizes, indent=2)+'\n')
            facts[label]['sizes'] = sizes
        orders = [['base', 'candidate'], ['candidate', 'base']] if args.benchmark else [['base', 'candidate']]
        for order, labels in enumerate(orders):
            for label in labels:
                raw = run([output/label/'schema_phase_probe', '--benchmark' if args.benchmark else '--check-only'], capture=True)
                report = json.loads(raw)
                if (not report['physical_close_zero_charge'] or report['repetitions'] != 3 or
                        report['timing_enabled'] != args.benchmark):
                    raise RuntimeError('qualification/cleanup failed for '+label)
                (output/label/('report-'+str(order)+'.json')).write_text(json.dumps(report, indent=2)+'\n')
    after = {label: inventory(root) for label, root in roots.items()}
    facts['source_after'] = after
    facts['source_unchanged'] = before == after
    facts['probe_unchanged'] = digest(machinery/'schema_phase_probe.cpp') == probe_hash
    if not facts['source_unchanged'] or not facts['probe_unchanged']:
        raise RuntimeError('source changed during probe build/run')
    facts['outputs_sha256'] = {str(p.relative_to(output)): digest(p) for p in sorted(output.rglob('*'))
                               if p.is_file() and (p.suffix in {'.ll', '.s', '.json', '.d'} or p.name == 'commands.txt')
                               and 'build' not in p.relative_to(output).parts and p.name != 'receipt.json'}
    (output/'receipt.json').write_text(json.dumps(facts, indent=2)+'\n')
    print(output/'receipt.json')


if __name__ == '__main__':
    main()
