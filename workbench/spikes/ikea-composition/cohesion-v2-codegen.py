#!/usr/bin/env python3
"""Audit actual callers across the disruptive cohesion revision.

Imports the strict ELF64 parser from cohesion-codegen.py. Only documented,
side-aware demangled API role/type correspondences are normalized. Every emitted
instruction/data byte, relocation offset/kind/value/addend remains literal.
Differences are findings to inspect, not failures to normalize away.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shlex
import subprocess

spec = importlib.util.spec_from_file_location('cohesion_codegen', Path(__file__).with_name('cohesion-codegen.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

PAIRS = [
    ('seriespack-ordinary', 'ikea/examples/seriespack/ordinary.cpp', 'ikea/examples/seriespack/ordinary.cpp'),
    ('seriespack-integration', 'ikea/examples/seriespack/integration.cpp', 'ikea/examples/seriespack/integration.cpp'),
    ('filters-composition', 'ikea/examples/filters/composition.cpp', 'ikea/examples/filters/composition.cpp'),
    ('streampack-ordinary', 'ikea/examples/streampack/ordinary.cpp', 'ikea/examples/streampack/ordinary.cpp'),
    ('tuplepack-ordinary', 'ikea/examples/tuplepack/ordinary.cpp', 'ikea/examples/tuplepack/ordinary.cpp'),
    ('tuplepack-words', 'ikea/examples/tuplepack/words.cpp', 'ikea/examples/tuplepack/words.cpp'),
    ('filters-ordinary', 'ikea/examples/filters/ordinary.cpp', 'ikea/examples/filters/ordinary.cpp'),
    ('seriespack-readers', 'ikea/src/seriespack/readers.cpp', 'ikea/src/seriespack/readers.cpp'),
    ('tuplepack-initialize', 'ikea/src/tuplepack/construction.cpp', 'ikea/src/tuplepack/initialize.cpp'),
    ('bec256-ordinary', 'ikea/examples/bec/ordinary.cpp', 'ikea/examples/bec/ordinary.cpp'),
    ('hashtable-ordinary', 'ikea/examples/hashtable/ordinary.cpp', 'ikea/examples/hashtable/ordinary.cpp'),
    ('selection', None, 'ikea/test/selection.cpp'),
]
base.SOURCE_FLAGS['ikea/examples/filters/ordinary.cpp'] = ['-fno-vectorize', '-fno-slp-vectorize']

# Apply to BEFORE identifiers only. These are correspondence labels, not claims
# that the corresponding body/layout is unchanged; actual bytes prove or refute it.
RENAMES = [
    ('ikea::block256', 'ikea::bitset256'),
    ('ikea::bec256::destination', 'ikea::bec256::view'),
    ('ikea::bec256::source', 'ikea::bec256::reader'),
    ('ikea::seriespack::composition::prepared_mutation<', 'ikea::seriespack::writer<'),
    ('ikea::seriespack::composition::mutation_operation<', 'ikea::seriespack::erased_writer<'),
    ('ikea::seriespack::composition::prepared<', 'ikea::seriespack::reader<'),
    ('ikea::seriespack::decoder<', 'ikea::seriespack::erased_reader<'),
    ('ikea::seriespack::bind_decoder<', 'ikea::seriespack::bind_reader<'),
    ('ikea::seriespack::row_selection', 'ikea::seriespack::selection'),
    ('ikea::seriespack::format<', 'ikea::seriespack::layout<'),
    ('ikea::seriespack::view<', 'ikea::seriespack::basic_view<'),
    ('ikea::seriespack::sum_change', 'ikea::seriespack::author::sum_change'),
    ('ikea::seriespack::no_summary', 'ikea::seriespack::author::no_summary'),
    ('ikea::seriespack::filter_values<', 'ikea::seriespack::author::filter_values<'),
    ('ikea::tuplepack::filter_values<', 'ikea::tuplepack::author::filter_values<'),
    ('ikea::seriespack::composition::', 'ikea::seriespack::author::'),
    ('ikea::tuplepack::read_operation<', 'ikea::tuplepack::reader<'),
    ('ikea::tuplepack::mutation_operation<', 'ikea::tuplepack::writer<'),
    ('ikea::tuplepack::construction_operation', 'ikea::tuplepack::writer<128u, 1u>'),
    ('ikea::tuplepack::constructor', 'ikea::tuplepack::write_plan<128u, 1u>'),
    ('ikea::tuplepack::native_reader<', 'ikea::tuplepack::author::native_reader<'),
    ('ikea::tuplepack::native_writer<', 'ikea::tuplepack::author::native_writer<'),
    ('ikea::tuplepack::erased_mutation<', 'ikea::tuplepack::erased_writer<'),
    ('ikea::tuplepack::field_filter<', 'ikea::tuplepack::predicate<'),
    ('ikea::source_write_journal', 'ikea::journal'),
    ('ikea::owner_write', 'ikea::journal::entry'),
    ('ikea::no_coverage', 'ikea::no_effects'),
    ('ikea::streampack::author::dispatch<', 'ikea::streampack::author::visit<'),
]
# Numeric control/code/map/input types are NOT mapped to std::byte. Only the
# final byte-object template argument of an admitted view/reader is changed.
BYTE_TEMPLATES = ('ikea::seriespack::basic_view<', 'ikea::tuplepack::basic_view<',
                  'ikea::tuplepack::reader<', 'ikea::tuplepack::author::native_reader<',
                  'ikea::tuplepack::predicate<',
                  'ikea::tuplepack::detail::field_predicate<')


def byte_argument(name, prefix):
    at = 0
    while (start := name.find(prefix, at)) >= 0:
        begin = start + len(prefix)
        depth, end = 1, begin
        while end < len(name) and depth:
            depth += (name[end] == '<') - (name[end] == '>')
            end += 1
        if depth:
            break
        payload = name[begin:end - 1]
        # Split top-level arguments, retaining nested layout expressions intact.
        args, first, nested = [], 0, 0
        for i, char in enumerate(payload):
            nested += (char == '<') - (char == '>')
            if char == ',' and not nested:
                args.append(payload[first:i]); first = i + 1
        args.append(payload[first:])
        args = [(a[:len(a)-len(a.lstrip())] + 'std::byte' if a.strip() == 'unsigned char' else
                 a[:len(a)-len(a.lstrip())] + 'std::byte const' if a.strip() == 'unsigned char const' else a) for a in args]
        replacement = ','.join(args)
        name = name[:begin] + replacement + name[end - 1:]
        at = begin + len(replacement) + 1
    return name


def chain_name(name):
    prefix = 'ikea::detail::native_chain<'
    while (start := name.find(prefix)) >= 0:
        begin = start + len(prefix)
        depth, end = 1, begin
        while end < len(name) and depth:
            depth += (name[end] == '<') - (name[end] == '>')
            end += 1
        if depth:
            break
        payload = name[begin:end-1]
        args, first, nested = [], 0, 0
        for i, char in enumerate(payload):
            nested += (char == '<') - (char == '>')
            if char == ',' and not nested:
                args.append(payload[first:i]); first = i + 1
        args.append(payload[first:])
        assert len(args) == 5, args
        # Old fourth parameter was the cold builder's per-module error type.
        # New shared chain owns chain_error; native carrier/slots/sequence stay.
        del args[3]
        name = name[:start] + 'ikea::author::chain<' + ','.join(args) + name[end-1:]
    return name


def normalize(name, source, arm):
    if arm != 'before':
        return name
    name = chain_name(name)
    # Old numeric-only reader/writer template parameters denote PLANS, unlike
    # the new reader's byte-object parameter and the new writer operation.
    name = re.sub(r'ikea::tuplepack::reader<([0-9]+u), ([0-9]+u)>',
                  r'ikea::tuplepack::read_plan<\1, \2>', name)
    name = re.sub(r'ikea::tuplepack::writer<([0-9]+u), ([0-9]+u)>',
                  r'ikea::tuplepack::write_plan<\1, \2>', name)
    for old, new in RENAMES:
        name = name.replace(old, new)
    for prefix in BYTE_TEMPLATES:
        name = byte_argument(name, prefix)
    # Hashtable's opaque value payload changed from numeric u8 storage to
    # std::byte. Restrict this correspondence to insert_slow's value argument;
    # numeric key/permutation/control types are not byte-object storage.
    if 'ikea::hashtable::table<' in name:
        name = name.replace('::insert_slow(unsigned long, std::array<unsigned char,',
                            '::insert_slow(unsigned long, std::array<std::byte,')
    # Advanced physical helpers moved as a unit; keep implementation/private
    # helpers distinct rather than pretending every old/new inline is a match.
    name = re.sub(r'ikea::seriespack::(?=(?:replace(?:_native16|_regions|_point|16)?(?:_unchecked)?|initialize(?:_unchecked)?|visit_writes(?:16|_point)|visit_storage)<)',
                  'ikea::seriespack::author::', name)
    return name


def sha(data):
    return hashlib.sha256(data).hexdigest()


def dump_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--baseline-commit', required=True)
    parser.add_argument('--compiler', default='clang++-21')
    parser.add_argument('--objdump', default='llvm-objdump-21')
    parser.add_argument('--cxxfilt', default='llvm-cxxfilt-21')
    parser.add_argument('--source', action='append', help='logical pair id; repeat to narrow')
    parser.add_argument('--profile', action='append', choices=base.PROFILES)
    parser.add_argument('--plan', action='store_true')
    parser.add_argument('--arm', choices=['before','after'])
    parser.add_argument('--compile-only', action='store_true', help='populate verified object cache without pairing')
    parser.add_argument('--reuse', action='store_true')
    parser.add_argument('--records-only', action='store_true',
                        help='re-emit strict records only; reject any unverified or absent object cache')
    parser.add_argument('--configured-release', action='store_true',
                        help='include configured Release debug info and diagnostic flags (-g -Wall -Wextra -Wpedantic)')
    args = parser.parse_args()
    pairs = [p for p in PAIRS if not args.source or p[0] in args.source]
    assert pairs and (not args.source or set(args.source) <= {p[0] for p in PAIRS})
    profiles = args.profile or list(base.PROFILES)
    trees = {'before': args.before.resolve(), 'after': args.after.resolve()}
    output = args.output.resolve()
    version = base.run([args.compiler, '--version'])
    assert 'clang version 21.1.8' in version
    jobs = []
    for profile in profiles:
        for logical, before, after in pairs:
            for arm, source in [('before', before), ('after', after)]:
                if source is None or (args.arm and arm != args.arm):
                    continue
                obj = output / profile / arm / (logical + '.o')
                cmd = base.command(args.compiler, trees[arm], source, profile, obj)
                if args.configured_release:
                    cmd[1:1] = ['-g', '-Wall', '-Wextra', '-Wpedantic']
                jobs.append((profile, logical, arm, source, obj, cmd))
    if args.plan:
        for *_, cmd in jobs:
            print(shlex.join(cmd))
        return
    output.mkdir(parents=True, exist_ok=True)
    receipt = {'scope': __doc__, 'baseline_commit': args.baseline_commit,
               'runner_sha256': sha(Path(__file__).read_bytes()),
               'elf_parser_sha256': sha(Path(__file__).with_name('cohesion-codegen.py').read_bytes()),
               'compiler_version': version, 'objdump_version': base.run([args.objdump, '--version']),
               'configured_release': args.configured_release,
               'source_flags': base.SOURCE_FLAGS, 'normalizations': {
                   'source_roots': 'both mapped to /sixdb by compiler; actual assertion strings remain literal',
                   'before_demangled_renames': RENAMES,
                   'before_numeric_only_TP_reader_writer': 'read_plan/write_plan, before operation renames',
                   'before_byte_object_template_parameters': BYTE_TEMPLATES,
                   'before_SP_physical_helpers': 'ordinary namespace -> author; private helpers not merged',
                   'before_HT_value_payload': 'insert_slow second argument std::array<unsigned char,N> -> std::array<std::byte,N>; numeric controls unchanged',
                   'before_shared_chain': 'detail::native_chain<Native,Mask,Slots,ModuleError,Sequence> -> author::chain<Native,Mask,Slots,Sequence>; explicit cold error type removed'},
               'compiles': [], 'pairs': []}
    records, dependencies = {}, {}
    def dependency_hash(path):
        if path not in dependencies:
            dependencies[path] = sha(Path(path).read_bytes())
        return dependencies[path]
    for profile, logical, arm, source, obj, cmd in jobs:
        obj.parent.mkdir(parents=True, exist_ok=True)
        cache = obj.with_suffix('.compile.json')
        previous = json.loads(cache.read_text()) if (args.reuse or args.records_only) and cache.exists() else None
        reused = bool(previous and previous['command'] == cmd and obj.exists() and
                      previous['object_sha256'] == sha(obj.read_bytes()) and
                      all(dependency_hash(d) == h for d, h in previous['dependencies'].items()))
        print(f'{"reuse verified" if reused else "compile"} {profile} {arm} {logical}', flush=True)
        if args.records_only and not reused:
            raise RuntimeError(f'--records-only requires a verified exact-command object cache: {obj}')
        if not reused:
            process = subprocess.run(cmd, text=True, capture_output=True)
            obj.with_suffix('.log').write_text(process.stdout + process.stderr)
            if process.returncode:
                print(process.stderr, flush=True)
                process.check_returncode()
        dep_text = obj.with_suffix('.d').read_text().replace('\\\n', ' ')
        deps = shlex.split(dep_text.split(':', 1)[1])
        hashes = {d: dependency_hash(d) for d in deps}
        compile_record = {'profile': profile, 'logical': logical, 'arm': arm,
                          'source': source, 'command': cmd, 'dependencies': hashes,
                          'object_sha256': sha(obj.read_bytes())}
        dump_json(cache, compile_record)
        receipt['compiles'].append(compile_record | {'reused': reused})
        obj.with_suffix('.s').write_text(base.run([args.objdump, '-d', '-r', '--demangle', str(obj)]))
        obj.with_suffix('.sections').write_text(base.run([args.objdump, '-s', str(obj)]))
        base.normalize = lambda name, source, arm=arm: normalize(name, source, arm)
        record = base.object_record(obj, source, args.cxxfilt)
        records[profile, logical, arm] = record
        dump_json(obj.with_suffix('.record.json'), record)
        dump_json(output / 'progress.json', {'completed': len(receipt['compiles']), 'total': len(jobs),
                                           'latest': [profile, logical, arm]})
    if args.compile_only:
        dump_json(output / ('compile-only-' + (args.arm or 'both') + '.json'), receipt)
        return
    assert not args.arm, '--arm requires --compile-only'
    for profile in profiles:
        for logical, before, after in pairs:
            left = records.get((profile, logical, 'before'))
            right = records[profile, logical, 'after']
            pair = {'profile': profile, 'logical': logical, 'before_source': before, 'after_source': after,
                    'before_object_sha256': left['sha256'] if left else None,
                    'after_object_sha256': right['sha256']}
            for kind in ['functions', 'constants', 'unwind']:
                rows = base.compare(left[kind] if left else {}, right[kind])
                pair[kind] = {'total': len(rows), 'equal': sum(r['equal'] for r in rows),
                              'before_only': sum(r['before_present'] and not r['after_present'] for r in rows),
                              'after_only': sum(r['after_present'] and not r['before_present'] for r in rows),
                              'changed_paired': sum(r['before_present'] and r['after_present'] and not r['equal'] for r in rows),
                              'differences': [r for r in rows if not r['equal']]}
            receipt['pairs'].append(pair)
            print(f'{profile} {logical}: '+', '.join(f"{k} {pair[k]['equal']}/{pair[k]['total']} exact" for k in ['functions','constants','unwind']), flush=True)
    receipt['dependency_sha256'] = dependencies
    dump_json(output / 'receipt.json', receipt)
    summary = {k:v for k,v in receipt.items() if k not in ['compiles','pairs','dependency_sha256']}
    summary['pairs'] = [{k: ({key:value for key,value in v.items() if key != 'differences'} if isinstance(v,dict) else v)
                         for k,v in row.items()} for row in receipt['pairs']]
    summary['dependency_manifest_sha256'] = sha(json.dumps(dependencies,sort_keys=True).encode())
    summary['dependency_paths'] = len(dependencies)
    summary['limits'] = 'Literal ELF object comparison, not linked placement or timings. Source/contract changes are real; unmatched or changed bodies require inspection. No body match is inferred merely from a renamed API.'
    dump_json(output / 'summary.json', summary)

if __name__ == '__main__':
    main()
