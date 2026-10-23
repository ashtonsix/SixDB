#!/usr/bin/env python3
"""Isolate three prepared-predicate representation controls against exact R4.

The direct-pointer control changes only its two borrowed pointer members and
accesses. The cached-tight controls keep those members and cache stride==1
at preparation, using predicate padding; cached-tight-address also preserves
that admitted unit-stride fact in the tight-path address. General reader stays 16 bytes and
predicate 24; API, admission, lifetime requirements and native kernels stay
the same. This compiles the maintained complete extra_tuple_2 consumer,
without a new timed harness.
"""
import argparse
import difflib
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess

SPEC = importlib.util.spec_from_file_location('handle_run', Path(__file__).with_name('run.py'))
run = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run)


def prepare(baseline, output, control):
    manifest = output / 'source-manifest.json'
    tree = output / 'source'
    if manifest.exists():
        record = json.loads(manifest.read_text())
        assert record['baseline'] == str(baseline)
        assert record.get('control', 'direct-pointers') == control
        for name, digest in record['sha256'].items():
            assert run.sha(tree / name) == digest, name
        return tree
    assert not tree.exists(), 'incomplete overlay: choose another output'
    shutil.copytree(baseline, tree)
    header = tree / 'ikea/include/ikea/tuplepack/filter.h'
    before = header.read_text()
    if control == 'direct-pointers':
        after = run.replace_once(before, '    reader<N, Byte, Rows> read_;\n',
            '    const read_plan<N, Rows>* plan_;\n    const basic_view<Byte>* source_;\n')
        after = run.replace_once(after,
            ': read_(source), code_(source.plan().controls().codes[0]) {}',
            ': plan_(&source.plan()), source_(&source.source()), code_(source.plan().controls().codes[0]) {}')
        after = after.replace('read_.size()', 'source_->size()')
        after = after.replace('read_.source()', '(*source_)')
        after = after.replace('read_.plan()', '(*plan_)')
        assert 'read_' not in after.replace('read_plan', '').replace('read_native', '').replace('read_bytes', '')
    else:
        assert control in ('cached-tight', 'cached-tight-address')
        after = run.replace_once(before, '    code code_;\n', '    code code_;\n    bool tight_;\n')
        after = run.replace_once(after,
            ': read_(source), code_(source.plan().controls().codes[0]) {}',
            ': read_(source), code_(source.plan().controls().codes[0]),\n          tight_(Rows >= 16 && source.source().stride() == 1) {}')
        after = run.replace_once(after, 'active == detail::all_rows<Rows> && read_.source().stride() == 1',
                                 'active == detail::all_rows<Rows> && tight_')
        if control == 'cached-tight-address':
            after = run.replace_once(after,
                'const auto* p = ikea::to_u8(read_.source().row_unchecked(first));',
                'const auto* p = ikea::to_u8(read_.source().row_unchecked(0)) + first;')
    header.write_text(after)
    run.save(manifest, {
        'scope': __doc__, 'baseline': str(baseline), 'control': control,
        'patch': ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
            fromfile='r4/ikea/include/ikea/tuplepack/filter.h',
            tofile=control + '/ikea/include/ikea/tuplepack/filter.h')),
        'sha256': {str(p.relative_to(tree)): run.sha(p) for p in tree.rglob('*') if p.is_file()},
    })
    return tree


def compact(receipt, output, baseline, baseline_objects):
    manifest = json.loads((output / 'source-manifest.json').read_text())
    result = {key: receipt[key] for key in ('scope', 'runner_sha256', 'source_manifest_sha256', 'compiler', 'helper_sha256')}
    result.update({'source_patch': manifest['patch'], 'control': receipt['control'],
        'baseline_recovery': 'R4 frozen after and configured final16 fixture objects in handle-boundaries.json; the three old controls remain immutable.',
        'limits': ['Isolated representation ablation, no live API/body changes and no native timing job.',
            'Same public prepared predicate remains two pointers plus copied code (24 bytes on the measured targets).',
            'Only the actual less_equal fixture is emitted here; this does not qualify every operator.',
            'Changed functions and unwind/exception records remain visible; no whole-object equality claim.'],
        'profiles': {}})
    for profile, comparison in receipt['comparison'].items():
        old_record = json.loads(Path(comparison['baseline_record']).read_text())
        new_record = json.loads((output / profile / 'extra_tuple_2.record.json').read_text())
        old_cache = json.loads((baseline_objects / profile / 'final16/extra_tuple_2.compile.json').read_text())
        new_cache = receipt['objects'][str(output / profile / 'extra_tuple_2.o')]
        item = {'before_object_sha256': comparison['baseline_object_sha256'],
                'after_object_sha256': comparison['after_object_sha256'], 'command': new_cache['command'],
                'exact_counts': {kind: {'same': len(comparison[kind]['same']), 'changed': len(comparison[kind]['changed'])}
                                 for kind in ('functions', 'constants', 'unwind')},
                'changed_functions': [], 'dependencies': {}}
        for name in comparison['functions']['changed']:
            b, a = old_record['functions'][name], new_record['functions'][name]
            item['changed_functions'].append({'name': name, 'before_bytes': b['size'], 'after_bytes': a['size'],
                'before_record_sha256': hashlib.sha256(json.dumps(b, sort_keys=True).encode()).hexdigest(),
                'after_record_sha256': hashlib.sha256(json.dumps(a, sort_keys=True).encode()).hexdigest()})
        project = {}
        for arm, cache, source in (('before', old_cache, baseline), ('after', new_cache, output / 'source')):
            project[arm], system = {}, {}
            for name, sha in cache['dependencies'].items():
                path = Path(name)
                if path.is_relative_to(source):
                    project[arm][str(path.relative_to(source))] = sha
                else:
                    system[name] = sha
            item['dependencies'][arm] = {kind: {'count': len(values),
                'sha256': hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()}
                for kind, values in (('project', project[arm]), ('system', system))}
        item['changed_project_dependencies'] = {name: [project['before'].get(name), project['after'].get(name)]
            for name in sorted(set(project['before']) | set(project['after']))
            if project['before'].get(name) != project['after'].get(name)}
        assert list(item['changed_project_dependencies']) == ['ikea/include/ikea/tuplepack/filter.h']
        path_record = output / profile / 'path.json'
        if path_record.exists():
            path = json.loads(path_record.read_text())
            actual = hashlib.sha256(json.dumps(new_record['functions'][path['function']], sort_keys=True).encode()).hexdigest()
            assert actual == path['literal_function_sha256']
            item['actual_full_tight_independent8_path'] = {'instructions': path['instructions'],
                'opcodes': path['opcodes'], 'conditions': len(path['conditional_branches']),
                'literal_function_sha256': path['literal_function_sha256'],
                'path_record_sha256': run.sha(path_record),
                'scope': 'Nonterminal full activity, tight placement, in-domain RHS; eight SIMD comparisons and existing index-opacity barrier.'}
        result['profiles'][profile] = item
    if 'native_check' in receipt:
        result['native_check'] = receipt['native_check']
    return result


def native_check(tree, output, library_cache, receipt):
    """Reuse only owning objects whose entire consumed source closure matches."""
    directory = output / 'native-check'
    objects, reused = [], []
    for name in ('producer', 'caller', 'check'):
        obj = directory / (name + '.o')
        run.compile_object(tree, str(run.STUDY / (name + '.cpp')), 'neon-v2', obj,
                           'final16', receipt)
        objects.append(obj)
    producer = json.loads((directory / 'producer.record.json').read_text())
    facts = bytes.fromhex(producer['constants']['.rodata.hb_facts']['bytes'])
    values = struct.unpack('<' + 'Q' * (len(facts) // 8), facts)
    census = {name: dict(zip(('size', 'alignment', 'trivial_copy', 'trivial_move',
                              'trivial_destructor', 'standard_layout'), values[i * 6:i * 6 + 6]))
              for i, name in enumerate(run.FACT_NAMES)}
    assert census['reader']['size'] == 16 and census['predicate']['size'] == 24
    for name in run.TP_SOURCES:
        source = 'ikea/src/tuplepack/' + name + '.cpp'
        if library_cache is None or not (library_cache / ('tuplepack-' + name + '.compile.json')).exists():
            obj = directory / ('tuplepack-' + name + '.o')
            run.compile_object(tree, source, 'neon-v2', obj, 'final16', receipt, module=True)
            objects.append(obj)
            continue
        old_obj = library_cache / ('tuplepack-' + name + '.o')
        cache_path = old_obj.with_suffix('.compile.json')
        old = json.loads(cache_path.read_text())
        assert run.sha(old_obj) == old['object_sha256']
        assert all(Path(p).exists() and run.sha(Path(p)) == sha for p, sha in old['dependencies'].items())
        old_tree = Path(old['command'][old['command'].index('-c') + 1]).parents[3]
        mapped = {str(tree / Path(p).relative_to(old_tree)) if Path(p).is_relative_to(old_tree) else p: sha
                  for p, sha in old['dependencies'].items()}
        changed = [p for p, sha in mapped.items() if not Path(p).exists() or run.sha(Path(p)) != sha]
        expected = run.command(tree, source, 'neon-v2', old_obj, 'final16', module=True)
        assert [arg.replace(str(old_tree), str(tree)) for arg in old['command']] == expected
        if changed:
            obj = directory / ('tuplepack-' + name + '.o')
            run.compile_object(tree, source, 'neon-v2', obj, 'final16', receipt, module=True)
        else:
            obj = old_obj
            reused.append({'source': source, 'original_cache': str(cache_path),
                'original_cache_sha256': run.sha(cache_path), 'object_sha256': run.sha(obj),
                'matched_dependency_count': len(mapped),
                'mapped_dependency_sha256': hashlib.sha256(json.dumps(mapped, sort_keys=True).encode()).hexdigest()})
            print('native library dependency-verified reuse', name, flush=True)
        objects.append(obj)
    binary = directory / 'check'
    command = ['clang++-21', '-Wl,--gc-sections', *map(str, objects), '-o', str(binary)]
    with (directory / 'link.log').open('w') as log:
        subprocess.run(command, cwd=run.ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    process = subprocess.run([str(binary)], capture_output=True, text=True)
    (directory / 'check.log').write_text(process.stdout + process.stderr)
    receipt['native_check'] = {'link': command, 'binary_sha256': run.sha(binary),
        'returncode': process.returncode, 'stdout': process.stdout, 'stderr': process.stderr,
        'census': {name: census[name] for name in ('reader', 'predicate', 'checked_reader', 'checked_filter')},
        'reused_library_objects': reused,
        'fresh_objects': {Path(name).name: {'object_sha256': row['object_sha256'],
            'source': row['source'], 'command_sha256': hashlib.sha256(json.dumps(row['command']).encode()).hexdigest(),
            'dependencies_count': len(row['dependencies']),
            'dependencies_sha256': hashlib.sha256(json.dumps(row['dependencies'], sort_keys=True).encode()).hexdigest()}
            for name, row in receipt['objects'].items() if Path(name).parent == directory},
        'flags_scope': 'Existing independent O3 native correctness witness; -gline-tables-only/-Werror, not configured throughput.'}
    run.save(output / 'receipt.json', receipt)
    process.check_returncode()
    print('independent native checks passed', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--baseline-objects', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--compact', type=Path)
    parser.add_argument('--control', choices=('direct-pointers', 'cached-tight', 'cached-tight-address'), default='direct-pointers')
    parser.add_argument('--native-check', action='store_true')
    parser.add_argument('--reuse-library', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tree = prepare(args.baseline.resolve(), out, args.control)
    if args.prepare_only:
        return
    receipt = {'scope': __doc__, 'control': args.control, 'runner_sha256': run.sha(Path(__file__)), 'objects': {},
               'source_manifest_sha256': run.sha(out / 'source-manifest.json'), 'comparison': {},
               'compiler': run.base.run(['clang++-21', '--version']),
               'helper_sha256': {p.name: run.sha(p) for p in
                   (Path(run.__file__), Path(run.helper.__file__), Path(run.base.__file__))}}
    for profile in run.base.PROFILES:
        obj = out / profile / 'extra_tuple_2.o'
        run.compile_object(tree, 'workbench/spikes/filter-composition/extra_tuple_2.cpp',
            profile, obj, 'final16', receipt, configured_release=True)
        old_path = args.baseline_objects / profile / 'final16/extra_tuple_2.record.json'
        cache = json.loads(old_path.with_name('extra_tuple_2.compile.json').read_text())
        assert run.sha(old_path.with_name('extra_tuple_2.o')) == cache['object_sha256']
        assert all(Path(p).exists() and run.sha(Path(p)) == sha for p, sha in cache['dependencies'].items())
        old = json.loads(old_path.read_text())
        new = json.loads(obj.with_suffix('.record.json').read_text())
        comparison = {'baseline_record': str(old_path), 'baseline_record_sha256': run.sha(old_path),
                      'baseline_object_sha256': old['sha256'], 'after_object_sha256': new['sha256']}
        for kind in ('functions', 'constants', 'unwind'):
            names = sorted(set(old[kind]) | set(new[kind]))
            comparison[kind] = {'same': [n for n in names if old[kind].get(n) == new[kind].get(n)],
                                'changed': [n for n in names if old[kind].get(n) != new[kind].get(n)]}
        receipt['comparison'][profile] = comparison
        run.save(out / 'receipt.json', receipt)
        print(profile, {k: (len(comparison[k]['same']), len(comparison[k]['changed']))
                        for k in ('functions', 'constants', 'unwind')}, flush=True)
    if args.native_check:
        native_check(tree, out, args.reuse_library.resolve() if args.reuse_library else None, receipt)
    if args.compact:
        run.save(args.compact, compact(receipt, out, args.baseline.resolve(), args.baseline_objects.resolve()))


if __name__ == '__main__':
    main()
