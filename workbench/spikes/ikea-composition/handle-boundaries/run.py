#!/usr/bin/env python3
"""Freeze three TP handle controls, inspect separate objects, then check native commands.

No timing loop, native kernel or live module API is changed. The generic16
control removes only reader's copied projection; predicate16 adds the projection
to the separately prepared private predicate instead. The actual extra_tuple_2
consumer is retained for a subsequent complete-caller comparison.
"""
import argparse
import difflib
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[4]
STUDY = Path('workbench/spikes/ikea-composition/handle-boundaries')
ARMS = ('cached24', 'generic16', 'predicate16')
TP_SOURCES = ('layout', 'initialize', 'prepare', 'kernels', 'gpr', 'routes',
              'packet_layout', 'packet_prepare', 'packet')
FACT_NAMES = ('reader', 'writer', 'native_reader', 'native_writer', 'filter_reader',
              'predicate', 'erased_writer', 'dense_reader', 'series_reader', 'series_writer',
              'erased_series_reader', 'erased_series_writer', 'read_plan', 'write_plan',
              'tuple_view', 'series_view', 'checked_reader', 'checked_writer',
              'checked_filter', 'checked_series_reader')
SPEC = importlib.util.spec_from_file_location('directory_codegen', Path(__file__).parents[1] / 'cohesion-directory-codegen.py')
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)
base = helper.base


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def replace_once(text, before, after):
    assert text.count(before) == 1, before
    return text.replace(before, after)


def prepare(source, out):
    sources = out / 'sources'
    manifest = out / 'source-manifest.json'
    if manifest.exists():
        result = json.loads(manifest.read_text())
        for arm, files in result['sha256'].items():
            for name, digest in files.items():
                assert sha(sources / arm / name) == digest, (arm, name)
        return result
    assert not sources.exists(), 'incomplete freeze: choose another output'
    result = {'source': str(source), 'scope': __doc__, 'sha256': {}, 'patches': {}}
    for arm in ARMS:
        tree = sources / arm
        shutil.copytree(source / 'ikea', tree / 'ikea')
        shutil.copytree(source / 'workbench/spikes/filter-composition',
                        tree / 'workbench/spikes/filter-composition')
        (tree / STUDY).mkdir(parents=True)
        for file in (ROOT / STUDY).glob('*'):
            if file.suffix in ('.h', '.cpp'):
                shutil.copy2(file, tree / STUDY / file.name)
        if arm != 'cached24':
            changes = []
            reader = tree / 'ikea/include/ikea/tuplepack/read.h'
            original = reader.read_text()
            text = replace_once(original, '    code filter_code_;\n', '')
            text = replace_once(text, ': plan_(&plan), source_(&source), filter_code_(plan.controls().codes[0]) {}',
                                ': plan_(&plan), source_(&source) {}')
            reader.write_text(text)
            changes.extend(difflib.unified_diff(original.splitlines(True), text.splitlines(True),
                                                fromfile='cached24/ikea/include/ikea/tuplepack/read.h',
                                                tofile=arm + '/ikea/include/ikea/tuplepack/read.h'))
            predicate = tree / 'ikea/include/ikea/tuplepack/filter.h'
            original = predicate.read_text()
            if arm == 'generic16':
                text = replace_once(original, 'const auto& code_ = read_.filter_code_;',
                                    'const auto& code_ = read_.plan().controls().codes[0];')
            else:
                text = replace_once(original, '    reader<N, Byte, Rows> read_;\n',
                                    '    reader<N, Byte, Rows> read_;\n    code filter_code_;\n')
                text = replace_once(text, ': read_(source) {}',
                                    ': read_(source), filter_code_(source.plan().controls().codes[0]) {}')
                text = replace_once(text, 'const auto& code_ = read_.filter_code_;',
                                    'const auto& code_ = filter_code_;')
            predicate.write_text(text)
            changes.extend(difflib.unified_diff(original.splitlines(True), text.splitlines(True),
                                                fromfile='cached24/ikea/include/ikea/tuplepack/filter.h',
                                                tofile=arm + '/ikea/include/ikea/tuplepack/filter.h'))
            if arm == 'predicate16':
                fixture = tree / 'workbench/spikes/filter-composition/extra.h'
                original = fixture.read_text()
                text = replace_once(original, 'using Filter = tp::reader<64, std::byte, 16>;',
                                    'using Filter = tp::detail::field_predicate<64, std::byte, 16>;')
                text = replace_once(text, '    const auto& filter = operation;',
                                    '    const auto filter = *Filter::bind(operation);')
                fixture.write_text(text)
                changes.extend(difflib.unified_diff(original.splitlines(True), text.splitlines(True),
                                                    fromfile='cached24/workbench/spikes/filter-composition/extra.h',
                                                    tofile=arm + '/workbench/spikes/filter-composition/extra.h'))
            result['patches'][arm] = ''.join(changes)
        result['sha256'][arm] = {str(p.relative_to(tree)): sha(p) for p in tree.rglob('*') if p.is_file()}
    save(manifest, result)
    return result


def prepare_final(source, out):
    """Keep the live endpoint proof separate from all three immutable controls."""
    manifest = out / 'final-source-manifest.json'
    tree = out / 'sources/final16'
    if manifest.exists():
        result = json.loads(manifest.read_text())
        assert result['source'] == str(source)
        for name, digest in result['sha256'].items():
            assert sha(tree / name) == digest, name
        return result
    assert not tree.exists(), 'incomplete final freeze: choose another output'
    shutil.copytree(source / 'ikea', tree / 'ikea')
    shutil.copytree(source / 'workbench/spikes/filter-composition',
                    tree / 'workbench/spikes/filter-composition')
    # The witness is exactly the isolated prepared-predicate witness except
    # for the now-supported public type spelling. Do not patch old controls.
    shutil.copytree(out / 'sources/predicate16' / STUDY, tree / STUDY)
    probe = tree / STUDY / 'probe.h'
    before = probe.read_text()
    after = replace_once(before, 'tp::detail::field_predicate<64, const std::byte, 16>',
                         'tp::predicate<64, const std::byte, 16>')
    probe.write_text(after)
    result = {'source': str(source),
              'probe_patch': ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                    fromfile='predicate16/probe.h', tofile='final16/probe.h')),
              'sha256': {str(p.relative_to(tree)): sha(p) for p in tree.rglob('*') if p.is_file()}}
    save(manifest, result)
    return result


def dependencies(obj):
    paths = shlex.split(obj.with_suffix('.d').read_text().replace('\\\n', ' ').split(':', 1)[1])
    return {name: sha(Path(name)) for name in paths}


def command(tree, source, profile, output, arm, *, ir=False, module=False, configured_release=False):
    result = base.command('clang++-21', tree, source, profile, output)
    if not module:
        result[1:1] = [f'-DHB_PREPARED_PREDICATE={int(arm in ("predicate16", "final16"))}']
    result[1:1] = (['-g', '-Wall', '-Wextra', '-Wpedantic'] if configured_release else
                   ['-gline-tables-only', '-Wall', '-Wextra', '-Wpedantic', '-Werror'])
    if source.endswith('extra_tuple_2.cpp'):
        result[1:1] = ['-DFILTER_SEAMS_NO_MAIN=1', '-fno-vectorize', '-fno-slp-vectorize']
    if ir:
        result[result.index('-c'):result.index('-c') + 1] = ['-S', '-emit-llvm']
        result[result.index('-MF') + 1] = str(output) + '.d'
    return result


def compile_object(tree, source, profile, obj, arm, receipt, *, module=False, configured_release=False):
    obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = command(tree, source, profile, obj, arm, module=module, configured_release=configured_release)
    cache = obj.with_suffix('.compile.json')
    previous = json.loads(cache.read_text()) if cache.exists() else None
    reusable = (previous and previous['command'] == cmd and obj.exists() and
                sha(obj) == previous['object_sha256'] and
                all(Path(p).exists() and sha(Path(p)) == digest for p, digest in previous['dependencies'].items()))
    print('object', profile, arm, source, 'reuse' if reusable else 'compile', flush=True)
    if not reusable:
        with obj.with_suffix('.log').open('w') as log:
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    obj.with_suffix('.s').write_text(base.run(['llvm-objdump-21', '-d', '-r', '--demangle', str(obj)]))
    record = helper.object_record(obj, source)
    save(obj.with_suffix('.record.json'), record)
    row = {'source': source, 'profile': profile, 'arm': arm, 'command': cmd,
           'configured_release': configured_release, 'object_sha256': sha(obj),
           'dependencies': dependencies(obj)}
    save(cache, row)
    receipt['objects'][str(obj)] = row
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='frozen R3 cohesion-inputs/after root')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', action='append', choices=base.PROFILES)
    parser.add_argument('--arm', action='append', choices=(*ARMS, 'final16'))
    parser.add_argument('--final-source', type=Path, help='final frozen source root for the separate final16 proof')
    parser.add_argument('--configured-release', action='store_true',
                        help='use configured Release -g and warnings instead of standalone line tables/-Werror')
    parser.add_argument('--stage', choices=('abi', 'check', 'fixture'), default='abi')
    args = parser.parse_args()
    out, source = args.output.resolve(), args.source.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = prepare(source, out)
    arms = args.arm or ARMS
    if 'final16' in arms:
        assert args.final_source, '--final-source is required for final16'
        prepare_final(args.final_source.resolve(), out)
    receipt_path = out / 'receipt.json'
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {'objects': {}, 'ir': {}, 'checks': {}}
    receipt.update({'scope': __doc__, 'runner_sha256': sha(Path(__file__)),
                    'compiler': base.run(['clang++-21', '--version']),
                    'source_manifest_sha256': sha(out / 'source-manifest.json'),
                    'helper_sha256': {p.name: sha(p) for p in
                                      [Path(helper.__file__), Path(base.__file__)]},
                    'limits': 'No timing claim. Separate TUs, noLTO. Raw metadata and payload ABI are distinguished; cold preparation and caller frames remain visible.'})
    receipt['flag_modes'] = {
        'standalone': 'Specified O3/-gline-tables-only/-Werror witness; not full configured Release.',
        'configured-release': 'Configured -O3 -g/-Wall/-Wextra/-Wpedantic, no NDEBUG or -Werror; explicit target/tune/source prefix-map substitutions retained.',
    }
    if 'final16' in arms:
        receipt['final_source_manifest_sha256'] = sha(out / 'final-source-manifest.json')
    profiles = args.profile or list(base.PROFILES)
    for profile in profiles:
        if args.stage == 'check' and profile != 'neon-v2':
            raise ValueError('native checks are local AArch64 only; cross objects are not executions')
        for arm in arms:
            tree = out / 'sources' / arm
            directory = out / ('configured-release' if args.configured_release else '') / profile / arm
            directory.mkdir(parents=True, exist_ok=True)
            compile_args = {'configured_release': args.configured_release}
            if args.stage == 'abi':
                for name in ('producer', 'caller', *(['series'] if arm == 'cached24' else [])):
                    compile_object(tree, str(STUDY / (name + '.cpp')), profile,
                                   directory / (name + '.o'), arm, receipt, **compile_args)
                    save(receipt_path, receipt)
                ir = directory / 'producer.ll'
                cmd = command(tree, str(STUDY / 'producer.cpp'), profile, ir, arm, ir=True, **compile_args)
                with ir.with_suffix('.ll.log').open('w') as log:
                    subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                text = ir.read_text()
                facts = re.search(r'@hb_facts = .*?constant \[\d+ x i64\] \[(.*?)\]', text)
                assert facts, 'literal census missing from IR'
                values = [int(n) for n in re.findall(r'i64 (\d+)', facts[1])]
                assert len(values) == len(FACT_NAMES) * 6
                facts = {name: dict(zip(('size', 'alignment', 'trivial_copy', 'trivial_move',
                                        'trivial_destructor', 'standard_layout'), values[i * 6:i * 6 + 6]))
                         for i, name in enumerate(FACT_NAMES)}
                ir_key = profile + '/' + arm + ('/configured-release' if args.configured_release else '')
                receipt['ir'][ir_key] = {'command': cmd, 'sha256': sha(ir),
                    'facts': facts, 'signatures': [line for line in text.splitlines()
                                                 if line.startswith(('define ', 'declare '))]}
                save(receipt_path, receipt)
                print('facts', profile, arm, {name: facts[name]['size'] for name in
                                             ('reader', 'predicate', 'checked_reader', 'writer', 'dense_reader')}, flush=True)
            elif args.stage == 'fixture':
                compile_object(tree, 'workbench/spikes/filter-composition/extra_tuple_2.cpp', profile,
                               directory / 'extra_tuple_2.o', arm, receipt, **compile_args)
                save(receipt_path, receipt)
            else:
                objects = []
                for name in ('producer', 'caller', 'check'):
                    obj = directory / (name + '.o')
                    compile_object(tree, str(STUDY / (name + '.cpp')), profile, obj, arm, receipt, **compile_args)
                    objects.append(obj)
                for name in TP_SOURCES:
                    obj = directory / ('tuplepack-' + name + '.o')
                    compile_object(tree, 'ikea/src/tuplepack/' + name + '.cpp', profile, obj, arm,
                                   receipt, module=True, **compile_args)
                    objects.append(obj)
                    save(receipt_path, receipt)
                binary = directory / 'check'
                link = ['clang++-21', '-Wl,--gc-sections', *map(str, objects), '-o', str(binary)]
                with (directory / 'link.log').open('w') as log:
                    subprocess.run(link, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                process = subprocess.run([str(binary)], capture_output=True, text=True)
                (directory / 'check.log').write_text(process.stdout + process.stderr)
                check_key = profile + '/' + arm + ('/configured-release' if args.configured_release else '')
                receipt['checks'][check_key] = {'link': link, 'binary_sha256': sha(binary),
                                                         'returncode': process.returncode,
                                                         'stdout': process.stdout, 'stderr': process.stderr}
                save(receipt_path, receipt)
                process.check_returncode()
                print('check passed', profile, arm, flush=True)
    save(receipt_path, receipt)


if __name__ == '__main__':
    main()
