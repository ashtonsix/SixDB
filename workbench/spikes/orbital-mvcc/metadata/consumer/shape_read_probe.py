#!/usr/bin/env python3
"""Actual shared-Shape read ABI, type sizes and separate-TU codegen; no timing."""
import argparse
import hashlib
import json
import platform
import re
import shlex
import shutil
import subprocess
from pathlib import Path


RELATIVE_CONSUMER = Path('workbench/spikes/orbital-mvcc/metadata/consumer')
CODE_SUFFIXES = {'.h', '.hpp', '.hh', '.cpp', '.cc', '.cxx', '.c', '.inc'}
ENDPOINTS = {
    'shape_read': {'admit_tuple_shape', 'read_checked', 'read_mapped_checked'},
    'shape_read_bridge': {'read_proven', 'read_mapped_proven'},
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_inventory(root):
    # Snapshot stability evidence, not a claim that unrelated code is executed.
    # Actual compiler inputs are separately recorded from dependency files.
    return {str(path.relative_to(root)): digest(path)
            for path in sorted(root.rglob('*'))
            if path.is_file() and path.suffix in CODE_SUFFIXES}


def signatures(ir, unit):
    result = {}
    for line in ir.splitlines():
        if not line.startswith('define '):
            continue
        match = re.search(r'@([^\s(]+)\(', line)
        if not match or not match.group(1).startswith('_ZN'):
            continue
        symbol = match.group(1)
        for name in ENDPOINTS[unit]:
            if f'10shape_read{len(name)}{name}E' in symbol:
                if name in result:
                    raise RuntimeError('duplicate ordinary endpoint definition: '+name)
                result[name] = {'unit': unit, 'symbol': symbol,
                                'ordinary_return': 'indirect' if 'sret(' in line else 'register',
                                'llvm_signature': line}
    if result.keys() != ENDPOINTS[unit]:
        raise RuntimeError('missing actual ordinary signatures for '+unit+': '+str(result.keys()))
    return result


def dependency_paths(path, source):
    text = path.read_text().replace('\\\n', ' ')
    if ':' not in text:
        raise RuntimeError('malformed compiler dependency file: '+str(path))
    names = shlex.split(text.split(':', 1)[1])
    return sorted({(Path(name) if Path(name).is_absolute() else source/ name).resolve()
                   for name in names})


def sections(raw):
    values = {match.group(1): int(match.group(2)) for line in raw.splitlines()
              if (match := re.match(r'^(\S+)\s+(\d+)\s+\d+\s*$', line))}
    if not values:
        raise RuntimeError('llvm-size returned no section rows')
    return {'text_bytes': sum(value for name, value in values.items() if name.startswith('.text')),
            'rodata_bytes': sum(value for name, value in values.items() if name.startswith('.rodata')),
            'sections': values}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True,
                        help='full immutable SixDB source snapshot, using relocated live include layout')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=2)
    args = parser.parse_args()
    source, output = args.source_root.resolve(), args.output.resolve()
    machinery = Path(__file__).resolve().parent
    consumer = source/RELATIVE_CONSUMER
    if output == source or source in output.parents:
        raise RuntimeError('output must be outside the immutable source snapshot')
    if platform.machine() not in {'aarch64', 'arm64'}:
        raise RuntimeError('run in the pinned Linux AArch64 environment; only x86 sizeof uses qemu')
    for name in ['shape_read.cpp', 'shape_read_bridge.cpp', 'shape_read.h', 'shape_read_private.h']:
        if not (consumer/name).is_file():
            raise RuntimeError('source snapshot is missing '+str(RELATIVE_CONSUMER/name))
    if not (consumer.parent/'orbital/shape/shape_private.h').is_file():
        raise RuntimeError('source snapshot is missing the authoritative private Shape header')

    before = source_inventory(source)
    machinery_before = {name: digest(machinery/name)
                        for name in ['shape_read_probe.py', 'shape_read_sizes.cpp']}
    output.mkdir(parents=True, exist_ok=True)
    probes = output/'probes'
    probes.mkdir(exist_ok=True)
    for name in machinery_before:
        shutil.copyfile(machinery/name, probes/name)
    size_probe = probes/'shape_read_sizes.cpp'
    commands, facts = [], {}

    def run(argv, capture=False):
        command = ['taskset', '-c', str(args.cpu), *argv]
        commands.append(shlex.join(command))
        (output/'commands.txt').write_text('\n'.join(commands)+'\n')
        return (subprocess.check_output(command, text=True, cwd=source) if capture else
                subprocess.run(command, check=True, cwd=source))

    compiler = '/usr/bin/clang++-21'
    version = run([compiler, '--version'], capture=True).strip()
    if '21.1.8' not in version:
        raise RuntimeError('use pinned Clang 21.1.8')
    spike = source/'workbench/spikes/orbital-mvcc'
    common = [compiler, '-std=c++23', '-O3', '-g0', '-UNDEBUG', '-Wall', '-Wextra', '-Wpedantic',
              '-fno-fast-math', '-ffp-contract=off', '-fdenormal-fp-math=ieee',
              '-DCONSUMER_METADATA_FROZEN=0', '-DSIXDB_TUNE_NEOVERSE_V3=0',
              '-I'+str(source), '-I'+str(source/'ikea/include'),
              '-I'+str(source/'orbital/include'), '-I'+str(spike/'baseline/include'),
              '-I'+str(spike), '-I'+str(source/'workbench/benchmarks/orbital')]
    gcc_cross = Path('/usr/lib/gcc-cross/x86_64-linux-gnu/15')
    if not gcc_cross.is_dir():
        raise RuntimeError('x86 GCC15 cross headers/libraries are required')
    targets = {
        'aarch64': ['--target=aarch64-linux-gnu', '-march=armv8-a+simd', '-mtune=neoverse-v2'],
        'x86_64': ['--target=x86_64-linux-gnu', '--gcc-install-dir='+str(gcc_cross), '--sysroot=/',
                   '-isystem', '/usr/x86_64-linux-gnu/include', '-march=x86-64-v3', '-mtune=znver5'],
    }
    (output/'source-before.json').write_text(json.dumps(before, indent=2)+'\n')
    for arch, flags in targets.items():
        dest = output/arch
        dest.mkdir(exist_ok=True)
        library = run([compiler, *flags, '-print-file-name=libstdc++.so'], capture=True).strip()
        if '/15/' not in library or not Path(library).is_file():
            raise RuntimeError('target is not using pinned libstdc++15: '+arch+' '+library)
        ordinary, object_sections, dependencies, no_global_ctors = {}, {}, {}, {}
        for unit in ENDPOINTS:
            for suffix, extra in [('ll', ['-S', '-emit-llvm']), ('s', ['-S']),
                                  ('o', ['-c', '-MMD', '-MF', str(dest/(unit+'.d'))])]:
                run([*common, *flags, str(consumer/(unit+'.cpp')), *extra,
                     '-o', str(dest/(unit+'.'+suffix))])
            ir = (dest/(unit+'.ll')).read_text()
            if 'llvm.global_ctors' in ir:
                raise RuntimeError('unexpected global initialization in '+unit)
            no_global_ctors[unit] = True
            ordinary.update(signatures(ir, unit))
            raw = run(['llvm-size-21', '-A', str(dest/(unit+'.o'))], capture=True)
            (dest/(unit+'-sections.txt')).write_text(raw)
            object_sections[unit] = sections(raw)
            dependencies[unit] = dependency_paths(dest/(unit+'.d'), source)

        run([*common, *flags, str(size_probe), '-S', '-emit-llvm', '-o', str(dest/'shape_read_sizes.ll')])
        if 'llvm.global_ctors' in (dest/'shape_read_sizes.ll').read_text():
            raise RuntimeError('unexpected global initialization in the sizeof-only probe')
        no_global_ctors['shape_read_sizes'] = True
        run([*common, *flags, '-ffunction-sections', '-fdata-sections', str(size_probe),
             '-MMD', '-MF', str(dest/'shape_read_sizes.d'), '-Wl,--gc-sections', '-o', str(dest/'sizes')])
        dependencies['shape_read_sizes'] = dependency_paths(dest/'shape_read_sizes.d', source)
        execution = [str(dest/'sizes')]
        if arch == 'x86_64':
            execution = ['qemu-x86_64', '-L', '/usr/x86_64-linux-gnu', *execution]
        sizes = json.loads(run(execution, capture=True))
        (dest/'sizes.json').write_text(json.dumps(sizes, indent=2)+'\n')

        dependency_hashes = {}
        for unit, paths in dependencies.items():
            recorded = {}
            for path in paths:
                if source in path.parents:
                    recorded[str(path.relative_to(source))] = digest(path)
                elif path == size_probe:
                    recorded['copied-probe/shape_read_sizes.cpp'] = digest(path)
                else:
                    raise RuntimeError('non-system compiler input escaped the snapshot: '+str(path))
            dependency_hashes[unit] = recorded
        (dest/'dependencies.json').write_text(json.dumps(dependency_hashes, indent=2)+'\n')
        facts[arch] = {
            'sizes': sizes, 'ordinary_signatures': ordinary, 'global_ctor_absent': no_global_ctors,
            'libstdcxx_search_result': library, 'object_sections': object_sections,
            'combined_two_object_unlinked_sum': {
                'text_bytes': sum(value['text_bytes'] for value in object_sections.values()),
                'rodata_bytes': sum(value['rodata_bytes'] for value in object_sections.values()),
            },
            'compiler_dependencies': dependency_hashes,
        }
        print(arch, 'actual shared-Shape ordinary ABI/sizes/code recorded', flush=True)

    after = source_inventory(source)
    (output/'source-after.json').write_text(json.dumps(after, indent=2)+'\n')
    if before != after:
        raise RuntimeError('archived source code changed during the probe')
    if any(digest(machinery/name) != value for name, value in machinery_before.items()):
        raise RuntimeError('probe machinery changed during execution')
    if any(digest(probes/name) != value for name, value in machinery_before.items()):
        raise RuntimeError('copied probe machinery changed during execution')
    receipt = {
        'scope': 'ordinary factory/four read ABIs, actual type sizes and separate-TU compiler output; no timings',
        'compiler': version, 'cxx_library': 'libstdc++15 both targets', 'cpu': args.cpu,
        'host': platform.platform(), 'source_root': str(source), 'consumer_metadata_frozen': 0,
        'working_directory': str(source), 'commands': 'commands.txt', 'facts': facts,
        'source_code_snapshot_hashes': before, 'source_unchanged_pre_post': True,
        'probe_hashes': machinery_before,
        'limits': ['only sizeof executables run: native ARM and qemu x86; never performance',
                   'combined code is the sum of two unlinked adapter objects, including emitted inline/local callees',
                   'external compact/Shape/Ikea/runtime helper bodies and link-time deduplication are excluded',
                   'type sizes do not establish optimized frames or concurrent path peaks',
                   'no capture, owner, publication, history, allocation, reclamation or LMDB work executes',
                   'snapshot hashes cover code files only; actual inputs are compiler dependency files, not documentation'],
        'output_hashes': {str(path.relative_to(output)): digest(path)
                          for path in sorted(output.rglob('*')) if path.is_file() and path.name != 'receipt.json'},
    }
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == '__main__':
    main()
