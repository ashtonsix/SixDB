#!/usr/bin/env python3
"""Compile actual captured performance callers for structural comparison only."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--local-output', action='store_true',
                        help='Also compile a private local-Outcome placement control')
    args = parser.parse_args()
    captures, output = args.captures.resolve(), args.output.resolve()
    study = Path(__file__).resolve().parent
    repo = study.parents[4]
    helper_path = repo / 'workbench/spikes/call-boundaries/access-interleaving/codegen.py'
    spec = importlib.util.spec_from_file_location('loom_chain_inspect', helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.relevant = lambda name: (
        name.startswith('orbital_versioned_performance::run_') or
        name.startswith('orbital_bench::Outcome orbital_versioned_performance::read_operation<') or
        name.startswith('orbital_versioned_performance::FusedTuple16::') or
        name.startswith('orbital_versioned_performance::ConsumedTuple16::') or
        'metadata_consumer::read_chain_into(' in name)
    helper.role = lambda name: 'whole_trial' if '::run_' in name else 'actual_operation'
    compiler = subprocess.check_output(['clang++-21', '--version'], text=True)
    if not re.search(r'\b21\.1\.8\b', compiler):
        raise ValueError('Pinned Clang21.1.8 required')
    output.mkdir(parents=True, exist_ok=False)
    # LMDB's C declarations are architecture-independent; do not add native
    # platform headers to the x86 sysroot search path.
    shutil.copyfile('/usr/include/lmdb.h', output / 'lmdb.h')
    receipt = {'compiler': compiler, 'recipe_sha256': sha(Path(__file__)),
        'helper_sha256': sha(helper_path), 'lmdb_header_sha256': sha(output / 'lmdb.h'),
        'variants': {}, 'limits': [helper.STACK_NOTE,
            'Actual captured run.cpp, not a reduced wrapper. Independent TUs; no LTO.',
            'Local cross-header compiler replica, distinct from the exact measured executable.',
            'No execution, PMU counters, instruction costs, latency or throughput measured.']}
    variants = ['direct16', 'consume']
    if args.local_output:
        variants.append('local-output')
    for tag in variants:
        capture_tag = 'consume' if tag == 'local-output' else tag
        capture = json.loads((captures / (capture_tag + '.json')).read_text())
        source = (repo / capture['source']).resolve()
        for name, digest in capture['files_sha256'].items():
            if sha(source / name) != digest:
                raise ValueError('Captured source changed: ' + name)
        placement_control = None
        if tag == 'local-output':
            private = output / 'local-output-source'
            shutil.copytree(source, private)
            source = private
            header = source / 'workbench/spikes/orbital-mvcc/metadata/consumer/operations.h'
            before = header.read_text()
            signature = ('inline void read_chain_into(const v::Schema& schema, ExplicitTuple16 state, Key key,\n'
                         '                            unsigned steps, orbital_bench::Outcome& output) {')
            initial = '  output = {};\n  if (!steps) return;'
            ending = '    key = record.next;\n  }\n}\ninline void read_chain8_into'
            if any(before.count(text) != 1 for text in (signature, initial, ending)):
                raise ValueError('Private placement control no longer matches captured operation')
            after = before.replace(signature, signature.replace('Outcome& output)', 'Outcome& final_output)'))
            after = after.replace(initial, '  orbital_bench::Outcome output{};\n'
                '  if (!steps) { final_output = output; return; }')
            after = after.replace(ending, '    key = record.next;\n  }\n'
                '  final_output = output;\n}\ninline void read_chain8_into')
            header.write_text(after)
            placement_control = {'changed_header': str(header),
                'before_sha256': hashlib.sha256(before.encode()).hexdigest(),
                'after_sha256': sha(header),
                'change': 'Accumulate an unescaped local Outcome and assign output once; search/decode/digest algorithm unchanged.'}
        directory = output / tag
        directory.mkdir()
        obj, ir, depfile = directory / 'run.o', directory / 'run.ll', directory / 'run.d'
        flags = ['--target=x86_64-linux-gnu', '--sysroot=/usr/x86_64-linux-gnu',
            '-isystem', '/usr/x86_64-linux-gnu/include/c++/15', '-isystem',
            '/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu',
            '-std=c++23', '-O3', '-g', '-Wall', '-Wextra', '-Wpedantic',
            '-fno-fast-math', '-ffp-contract=off', '-fdenormal-fp-math=ieee',
            '-fvisibility=hidden', '-fvisibility-inlines-hidden',
            '-ffunction-sections', '-fdata-sections', '-march=x86-64-v3', '-mtune=znver5',
            '-DSIXDB_TUNE_GENERIC=0', '-DSIXDB_TUNE_GRANITE_RAPIDS=0',
            '-DSIXDB_TUNE_NEOVERSE_V2=0', '-DSIXDB_TUNE_NEOVERSE_V3=0', '-DSIXDB_TUNE_ZEN5=1',
            '-I' + str(output)]
        flags += ['-I' + str(source / p) for p in ('orbital/include', 'ikea/include',
            'workbench/benchmarks/orbital', 'workbench/spikes/orbital-mvcc/baseline/include',
            'workbench/spikes/orbital-mvcc/consumer', 'workbench/spikes/orbital-mvcc')]
        cpp = source / 'workbench/spikes/orbital-mvcc/performance/run.cpp'
        command = ['clang++-21', *flags, '-MD', '-MF', str(depfile), '-c', str(cpp), '-o', str(obj)]
        subprocess.run(command, check=True)
        deps = {str(p): sha(p) for p in helper.dependencies(depfile, repo)}
        ir_command = ['clang++-21', *flags, '-S', '-emit-llvm', str(cpp), '-o', str(ir)]
        subprocess.run(ir_command, check=True)
        inspected = helper.inspect(obj, 'x86')
        receipt['variants'][tag] = {'job': capture['job'], 'source': str(source),
            'source_archive_sha256': capture['archive_sha256'], 'argv': command,
            'ir_argv': ir_command, 'object_sha256': sha(obj), 'ir_sha256': sha(ir),
            'dependency_sha256': deps, 'private_placement_control': placement_control, **inspected}
        for path, digest in deps.items():
            if sha(Path(path)) != digest:
                raise ValueError('Dependency changed during compilation: ' + path)
        print(tag + ': actual whole caller compiled and dependencies verified', flush=True)
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
