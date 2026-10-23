#!/usr/bin/env python3
"""Inspect a copied one-code predicate over frozen R4, without live API changes.

The isolated handle retains only its named view pointer and {offset,shift,width}.
Scalar and full-tight algorithms stay encoded; the fallback reuses the existing
one-byte gather and compares encoded bytes rather than generic decoded packets.
Current qualified sparse64 NEON/common-allocation behavior is preserved pending
the independent access-contract audit. This is emitted-code evidence, not timing.
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
HEADER = 'ikea/include/ikea/tuplepack/filter.h'
SOURCES = ('producer', 'caller', 'extra_tuple_2')
SHARED_FILES = (
    'ikea/include/ikea/tuplepack/initialize.h',
    'ikea/include/ikea/streampack/write.h',
    'ikea/test/tuplepack/operations.cpp',
    'ikea/include/ikea/tuplepack/author/native.h',
    'ikea/include/ikea/tuplepack/author/execution.h',
    'ikea/include/ikea/tuplepack/detail/native/packet.h',
    'ikea/include/ikea/tuplepack/detail/native/read.h',
    'ikea/test/tuplepack/access.cpp', 'ikea/CMakeLists.txt',
    'ikea/test/filters/tuple.cpp', 'ikea/examples/filters/ordinary.cpp',
    'workbench/spikes/filter-composition/mask-codegen.cpp',
)

FALLBACK = '''        // One admitted code needs no packet-order shuffle. The existing
        // transfer machinery retains tight/masked/common-window paths and
        // exact active-byte gathers for general strided placement.
        detail::packet_placement place;
        place.first = code_.offset;
        place.count = 1;
        const auto base = ikea::to_u8(source_->row_unchecked(first));
        const auto stride = source_->stride();
        const auto value = native::packet_detail::gather_bound<Rows>(
            place, [&](unsigned row) { return base + row * stride; }, active, stride);
        const auto mask = limit << code_.shift;
        const auto query = rhs << code_.shift;
#if defined(__aarch64__)
        std::uint64_t bits = ikea::detail::filter::bits<1, Op>(
            vandq_u8(value.a, vdupq_n_u8(mask)), query);
        if constexpr (Rows > 16)
            bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                vandq_u8(value.b, vdupq_n_u8(mask)), query)) << 16;
        if constexpr (Rows > 32) {
            bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                vandq_u8(value.c, vdupq_n_u8(mask)), query)) << 32;
            bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                vandq_u8(value.d, vdupq_n_u8(mask)), query)) << 48;
        }
#elif defined(__AVX512VBMI__)
        std::uint64_t bits;
        if constexpr (Rows <= 16)
            bits = ikea::detail::filter::bits<1, Op>(
                _mm_and_si128(_mm512_castsi512_si128(value), _mm_set1_epi8(mask)), query);
        else if constexpr (Rows <= 32)
            bits = ikea::detail::filter::bits<1, Op>(
                _mm256_and_si256(_mm512_castsi512_si256(value), _mm256_set1_epi8(mask)), query);
        else
            bits = ikea::detail::filter::bits<1, Op>(
                _mm512_and_si512(value, _mm512_set1_epi8(mask)), query);
#else
        std::uint64_t bits;
        if constexpr (Rows <= 16)
            bits = ikea::detail::filter::bits<1, Op>(
                _mm_and_si128(_mm256_castsi256_si128(value.a), _mm_set1_epi8(mask)), query);
        else {
            bits = ikea::detail::filter::bits<1, Op>(
                _mm256_and_si256(value.a, _mm256_set1_epi8(mask)), query);
            if constexpr (Rows > 32)
                bits |= std::uint64_t(ikea::detail::filter::bits<1, Op>(
                    _mm256_and_si256(value.b, _mm256_set1_epi8(mask)), query)) << 32;
        }
#endif
        return bits & active;
'''


def render(before):
    after = run.replace_once(before, '    reader<N, Byte, Rows> read_;\n',
                             '    const basic_view<Byte>* source_;\n')
    after = run.replace_once(after,
        ': read_(source), code_(source.plan().controls().codes[0]) {}',
        ': source_(&source.source()), code_(source.plan().controls().codes[0]) {}')
    after = after.replace('read_.size()', 'source_->size()').replace('read_.source()', '(*source_)')
    after = run.replace_once(after,
        '        return author::filter_values<Op, N>(detail::read_native(read_.plan(), (*source_), first, active), rhs, active);\n',
        FALLBACK)
    after = run.replace_once(after,
        "/// reader's plan/view pointers and the code projection, so the reader wrapper\n"
        '/// may expire. The named plan/view and their values stay stable through use.\n'
        '/// Prepare once for repeated filters; general readers retain only two pointers.\n',
        '/// code projection and named view pointer. Plan/reader may expire after bind;\n'
        '/// the named view and its values stay stable through use. Isolated control.\n')
    assert 'read_.' not in after
    return after


def prepare(baseline, output):
    manifest_path, tree = output / 'source-manifest.json', output / 'source'
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        assert manifest['baseline'] == str(baseline)
        assert all(run.sha(tree / p) == h for p, h in manifest['after_sha256'].items())
        assert (tree / HEADER).read_text() == render((baseline / HEADER).read_text())
        assert manifest['before_sha256'][HEADER] == run.sha(baseline / HEADER)
        return tree, manifest
    assert not tree.exists(), 'choose fresh output; never mutate an old control'
    original_hashes = {str(p.relative_to(baseline)): run.sha(p)
                       for p in baseline.rglob('*') if p.is_file()}
    shutil.copytree(baseline, tree)
    header = tree / HEADER
    before = header.read_text()
    after = render(before)
    header.write_text(after)
    hashes = {str(p.relative_to(tree)): run.sha(p) for p in tree.rglob('*') if p.is_file()}
    assert [p for p in hashes if hashes[p] != original_hashes.get(p)] == [HEADER]
    manifest = {'scope': __doc__, 'baseline': str(baseline),
        'generator_sha256': run.sha(Path(__file__)),
        'before_sha256': original_hashes, 'after_sha256': hashes,
        'changed_files': {HEADER: [original_hashes[HEADER], hashes[HEADER]]},
        'patch': ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
            fromfile='r4/' + HEADER, tofile='projection/' + HEADER))}
    run.save(manifest_path, manifest)
    return tree, manifest


def prepare_final_pair(baseline, shared, output):
    """Freeze both corrected access arms; only the final predicate header differs."""
    manifest_path = output / 'manifest.json'
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        assert manifest['baseline'] == str(baseline) and manifest['shared_source'] == str(shared)
        for arm, hashes in manifest['source_sha256'].items():
            assert all(run.sha(output / arm / p) == h for p, h in hashes.items())
        return manifest
    assert not (output / 'before').exists() and not (output / 'after').exists()
    before = output / 'before'
    shutil.copytree(baseline, before)
    shared_changes = {}
    for name in SHARED_FILES:
        target, source = before / name, shared / name
        old = run.sha(target) if target.exists() else None
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        shared_changes[name] = {'r4_sha256': old, 'shared_sha256': run.sha(target)}
    patches = {}

    def edit(name, old, new):
        path = before / name
        original = path.read_text()
        final = run.replace_once(original, old, new)
        path.write_text(final)
        patches[name] = ''.join(difflib.unified_diff(original.splitlines(True), final.splitlines(True),
            fromfile='r4/' + name, tofile='shared/' + name))

    edit('ikea/include/ikea/tuplepack/read.h',
         'template <unsigned N, class Byte, unsigned Rows> class predicate;\n', '')
    edit('workbench/spikes/filter-composition/extra.h',
         'using Filter = tp::predicate<64, std::byte, 16>;',
         'using Filter = typename decltype(tp::bind_predicate(std::declval<const tp::reader<64, std::byte, 16>&>()))::value_type;')
    edit('ikea/test/filters/tuple.cpp',
         '                // Prepared predicates retain the shared plan/view, not this\n'
         '                // intermediate reader wrapper. Both forms use the same bytes.\n',
         '                // The intermediate reader wrapper may expire after binding.\n'
         '                // Both prepared forms use the same bytes.\n')
    # One common benchmark/ABI source compiles against both public type shapes.
    for name in ('blocks.cpp', 'probe.h', 'producer.cpp', 'caller.cpp', 'check.cpp'):
        source = (baseline / run.STUDY / name) if name != 'blocks.cpp' else shared / run.STUDY / name
        if not source.exists():
            source = shared / run.STUDY / name
        target = before / run.STUDY / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    probe = (before / run.STUDY / 'probe.h').read_text()
    old_alias = next(line for line in probe.splitlines() if line.startswith('using predicate ='))
    assert old_alias in ('using predicate = tp::predicate<64, const std::byte, 16>;',
                         'using predicate = tp::detail::field_predicate<64, const std::byte, 16>;')
    edit(str(run.STUDY / 'probe.h'), old_alias,
         'using predicate = typename decltype(tp::bind_predicate(std::declval<const filter_reader&>()))::value_type;')
    blocks_alias = 'using Predicate = typename decltype(tp::bind_predicate(std::declval<const Reader&>()))::value_type;'
    assert blocks_alias in (before / run.STUDY / 'blocks.cpp').read_text()
    shutil.copytree(before, output / 'after')
    original = (before / HEADER).read_text()
    final = render(original)
    final = run.replace_once(final,
        'template <unsigned N, class Byte, unsigned Rows = 1> class predicate {',
        'template <class Byte, unsigned Rows = 1> class predicate {\n'
        '    static_assert(Rows <= 64 && std::has_single_bit(Rows));')
    final = run.replace_once(final, '    explicit predicate(const reader<N, Byte, Rows>& source)',
        '    template <unsigned N>\n    explicit predicate(const reader<N, Byte, Rows>& source)')
    final = run.replace_once(final, '    [[nodiscard]] static std::expected<predicate, error>\n',
        '    template <unsigned N>\n    [[nodiscard]] static std::expected<predicate, error>\n')
    final = final.replace('predicate<N, Byte, Rows>', 'predicate<Byte, Rows>')
    final = run.replace_once(final,
        'template <unsigned N, class Byte, unsigned Rows>\n'
        'auto bind_predicate(const read_plan<N, Rows>&&, const basic_view<Byte>&) = delete;\n', '')
    final = final.replace('the named view and its values stay stable through use. Isolated control.',
        'the named view and its values stay stable through use. No plan is retained.')
    final = final.replace('(*source_).', 'source_->')
    final = run.replace_once(final, '#pragma once\n',
        '#pragma once\n#include <bit>\n#include <cstddef>\n#include <cstdint>\n'
        '#include <expected>\n#include <utility>\n')
    final = run.replace_once(final, '#include <ikea/tuplepack/author/filter.h>',
                             '#include <ikea/detail/filter/compare.h>')
    final = run.replace_once(final, '#include <ikea/tuplepack/detail/native/read.h>',
                             '#include <ikea/tuplepack/detail/native/packet.h>')
    final = run.replace_once(final,
        '        // transfer machinery retains tight/masked/common-window paths and\n'
        '        // exact active-byte gathers for general strided placement.\n',
        '        // transfer machinery retains tight and masked paths, with exact\n'
        '        // active-byte gathers for strided placement and partial windows.\n')
    (output / 'after' / HEADER).write_text(final)
    hashes = {arm: {str(p.relative_to(output / arm)): run.sha(p)
        for p in (output / arm).rglob('*') if p.is_file()} for arm in ('before', 'after')}
    differing = {p: [hashes['before'].get(p), hashes['after'].get(p)]
        for p in sorted(hashes['before'].keys() | hashes['after'].keys())
        if hashes['before'].get(p) != hashes['after'].get(p)}
    assert list(differing) == [HEADER]
    manifest = {'scope': 'Final reduced predicate paired source, not the historical R4 diagnostic.',
        'baseline': str(baseline), 'shared_source': str(shared),
        'runner_sha256': run.sha(Path(__file__)), 'shared_files': shared_changes,
        'shared_patches': patches, 'source_sha256': hashes,
        'source_manifest_sha256': {arm: hashlib.sha256(json.dumps(h, sort_keys=True).encode()).hexdigest()
                                   for arm, h in hashes.items()},
        'different_files': differing,
        'candidate_patch': ''.join(difflib.unified_diff(original.splitlines(True), final.splitlines(True),
            fromfile='before/' + HEADER, tofile='after/' + HEADER)),
        'access_scope': 'Both arms use strict active-unit defaults; explicit common-window authored controls remain separate.',
        'endpoint': 'predicate<Byte,Rows>; N-templated factories; copied code and named-view pointer; temporary plans accepted, temporary views rejected.'}
    run.save(manifest_path, manifest)
    return manifest


def checked_cache(path):
    data = json.loads(path.read_text())
    assert data['configured_release'], path
    assert run.sha(path.with_suffix('').with_suffix('.o')) == data['object_sha256']
    assert all(Path(p).exists() and run.sha(Path(p)) == h for p, h in data['dependencies'].items())
    return data


def census(record):
    facts = bytes.fromhex(record['constants']['.rodata.hb_facts']['bytes'])
    values = struct.unpack('<' + 'Q' * (len(facts) // 8), facts)
    return {n: dict(zip(('size', 'alignment', 'trivial_copy', 'trivial_move',
                        'trivial_destructor', 'standard_layout'), values[i * 6:i * 6 + 6]))
            for i, n in enumerate(run.FACT_NAMES)}


def analyse(output, baseline_objects):
    """Retain full-function observations and the existing qualified path recipe."""
    helpers = {}
    for name in ('analyse', 'paths'):
        spec = importlib.util.spec_from_file_location('projection_' + name, Path(__file__).with_name(name + '.py'))
        helpers[name] = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helpers[name])
    receipt = json.loads((output / 'receipt.json').read_text())
    baseline_paths = json.loads((baseline_objects.parent / 'tight-paths.json').read_text())
    summary = {k: receipt[k] for k in ('scope', 'changed_files', 'source_patch', 'compiler', 'limits')}
    summary.update({'analysis_runner_sha256': run.sha(Path(__file__)),
        'source_manifest_sha256': receipt['source_manifest_sha256'], 'profiles': {},
        'analysis_helper_sha256': {name: run.sha(Path(mod.__file__)) for name, mod in helpers.items()}})
    for profile, files in receipt['profiles'].items():
        item = {'objects': {}, 'whole_function_observations': {}}
        for name, row in files.items():
            item['objects'][name] = {k: row[k] for k in ('before_object_sha256', 'after_object_sha256',
                'command', 'dependency_digests', 'changed_project_dependencies', 'source_path_correspondence')}
            item['objects'][name]['strict_counts'] = {kind: {key: len(value) for key, value in row[kind].items()}
                                                     for kind in ('functions', 'constants', 'unwind')}
            if 'census' in row:
                item['census'] = {arm: {n: facts[n] for n in ('reader', 'predicate', 'checked_reader', 'checked_filter')}
                                  for arm, facts in row['census'].items()}
            for arm, path in (('before', baseline_objects / profile / 'final16' / name),
                              ('after', output / profile / name)):
                record = json.loads(path.with_suffix('.record.json').read_text())
                assembly = helpers['analyse'].functions(path.with_suffix('.s'))
                for function, body in record['functions'].items():
                    if not function.startswith(('hb::consume_filter', 'hb::pass_predicate', 'hb::make_filter',
                        'hb::crowded_filters', 'hb::checked_filter_command', 'void runtime::measure<')):
                        continue
                    signature = function.split(' [symbol:', 1)[0]
                    rows = assembly[signature]
                    item['whole_function_observations'].setdefault(signature, {})[arm] = {
                        'bytes': body['size'], 'instructions': len(rows),
                        'literal_function_sha256': hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest(),
                        'stack_adjustments': [r for r in rows if helpers['analyse'].stack_adjustment(r)],
                        'stack_accesses': [r for r in rows if ('[sp' in r if profile == 'neon-v2' else '%rsp' in r)],
                        'calls': [r for r in rows if r.startswith(('bl\t', 'callq\t'))]}
        path = helpers['paths'].trace(output / profile / 'extra_tuple_2.record.json', profile)
        run.save(output / profile / 'path.json', path)
        old_path = baseline_paths['configured-release/' + profile + '/final16']
        item['full_tight_independent8_path'] = {arm: {k: p[k] for k in
            ('instructions', 'opcodes', 'literal_function_sha256')}
            for arm, p in (('before', old_path), ('after', path))}
        item['full_tight_independent8_path']['scope'] = path['scope']
        summary['profiles'][profile] = item
    summary['interpretation'] = [
        'The one-byte placement aggregate is scalarized away: leaf consume_filter has no calls; NEON stack has only saved registers, x86 has one8B scalar spill, not a packet_placement object.',
        'Generic fallback/control-route code shrinks substantially; whole-function totals include mutually exclusive paths and are not executed instruction counts.',
        'The selected full-tight width5 path improves NEON/AVX2 static counts but regresses AVX512; preserve this negative rather than infer speed from16B.',
        'Historical R4 sparse64 common-window behavior is retained only in this diagnostic. Final public endpoint sources share a separate strict access correction.']
    run.save(output / 'summary.json', summary)
    return summary


def reproof(pair, output, check_source):
    manifest = json.loads((pair / 'manifest.json').read_text())
    for arm, hashes in manifest['source_sha256'].items():
        assert all(run.sha(pair / arm / p) == h for p, h in hashes.items())
    receipt_path = output / 'receipt.json'
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {'objects': {}}
    receipt.update({'scope': 'Configured final public endpoint ABI and complete caller; no native timing.',
        'source_manifest_sha256': run.sha(pair / 'manifest.json'), 'runner_sha256': run.sha(Path(__file__)),
        'source_pair': str(pair), 'compiler': run.base.run(['clang++-21', '--version'])})
    for profile in run.base.PROFILES:
        for arm in ('before', 'after'):
            tree = pair / arm
            for name in SOURCES:
                source = ('workbench/spikes/filter-composition/extra_tuple_2.cpp' if name == 'extra_tuple_2'
                          else str(run.STUDY / (name + '.cpp')))
                obj = output / profile / arm / (name + '.o')
                run.compile_object(tree, source, profile, obj, 'final16', receipt, configured_release=True)
                run.save(receipt_path, receipt)
    if check_source:
        directory, tree = output / 'native-check', pair / 'after'
        directory.mkdir(parents=True, exist_ok=True)
        wrapper = directory / 'projection-check-wrapper.cpp'
        text = '#define IKEA_PROJECTION_CANDIDATE 1\n#include ' + json.dumps(str(check_source)) + '\n'
        if wrapper.exists():
            assert wrapper.read_text() == text
        else:
            wrapper.write_text(text)
        objects = []
        check = directory / 'projection-check.o'
        run.compile_object(tree, str(wrapper), 'neon-v2', check, 'final16', receipt,
                           configured_release=True)
        objects.append(check)
        run.save(receipt_path, receipt)
        for name in run.TP_SOURCES:
            obj = directory / ('tuplepack-' + name + '.o')
            run.compile_object(tree, 'ikea/src/tuplepack/' + name + '.cpp', 'neon-v2', obj,
                               'final16', receipt, configured_release=True, module=True)
            objects.append(obj)
            run.save(receipt_path, receipt)
        binary = directory / 'projection-check'
        command = ['clang++-21', '-Wl,--gc-sections', *map(str, objects), '-o', str(binary)]
        with (directory / 'link.log').open('w') as log:
            subprocess.run(command, cwd=run.ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        process = subprocess.run([str(binary)], capture_output=True, text=True)
        (directory / 'check.log').write_text(process.stdout + process.stderr)
        receipt['native_check'] = {'source': str(check_source), 'source_sha256': run.sha(check_source),
            'link': command, 'binary_sha256': run.sha(binary), 'returncode': process.returncode,
            'stdout': process.stdout, 'stderr': process.stderr,
            'flags_scope': 'All ten objects freshly compiled with configured Release flags; local NEON correctness only.'}
        run.save(receipt_path, receipt)
        process.check_returncode()
        print('Final public endpoint independent native check passed', flush=True)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--baseline-objects', type=Path,
                        help='configured-release profile/final16 directory root')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--final-pair', action='store_true', help='prepare only the final corrected paired source')
    parser.add_argument('--shared-source', type=Path, help='source of explicit shared corrections and blocks witness')
    parser.add_argument('--analyse-only', action='store_true')
    parser.add_argument('--reproof-pair', type=Path)
    parser.add_argument('--check-source', type=Path)
    parser.add_argument('--profile', action='append', choices=run.base.PROFILES)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.reproof_pair:
        reproof(args.reproof_pair.resolve(), output, args.check_source.resolve() if args.check_source else None)
        return
    if args.analyse_only:
        assert args.baseline_objects
        analyse(output, args.baseline_objects.resolve())
        return
    assert args.baseline
    baseline = args.baseline.resolve()
    if args.final_pair:
        assert args.shared_source
        manifest = prepare_final_pair(baseline, args.shared_source.resolve(), output)
        print(json.dumps({'manifest_sha256': run.sha(output / 'manifest.json'),
            'source_manifest_sha256': manifest['source_manifest_sha256'],
            'different_files': manifest['different_files']}, indent=2), flush=True)
        return
    assert args.baseline_objects
    tree, manifest = prepare(baseline, output)
    if args.prepare_only:
        print(tree)
        return
    receipt_path = output / 'receipt.json'
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {'objects': {}, 'profiles': {}}
    receipt.update({'scope': __doc__, 'runner_sha256': run.sha(Path(__file__)),
        'source_manifest_sha256': run.sha(output / 'source-manifest.json'),
        'source_patch': manifest['patch'], 'changed_files': manifest['changed_files'],
        'baseline_recovery': 'R4 after source, public final16 bridge from handle-boundaries.json; no later live API delta.',
        'helper_sha256': {Path(p).name: run.sha(Path(p)) for p in
            (run.__file__, run.helper.__file__, run.base.__file__)},
        'compiler': run.base.run(['clang++-21', '--version']),
        'limits': ['No live edit, native timing, worker launch or adoption decision.',
            'One complete less_equal width5/8 fixture plus separate raw/checked/crowded caller/callee.',
            'Existing qualified NEON common-window behavior retained, not an active-unit-only promise.',
            'Raw16B and checked24B ABI differ; expected still uses a memory return.',
            'Objects are unlinked; layout/predictor/cache/timing effects remain unqualified.']})
    for profile in args.profile or run.base.PROFILES:
        results = {}
        for name in SOURCES:
            old_cache_path = args.baseline_objects / profile / 'final16' / (name + '.compile.json')
            old_cache = checked_cache(old_cache_path)
            source = ('workbench/spikes/filter-composition/extra_tuple_2.cpp' if name == 'extra_tuple_2'
                      else str(run.STUDY / (name + '.cpp')))
            obj = output / profile / (name + '.o')
            new_cache = run.compile_object(tree, source, profile, obj, 'final16', receipt,
                                           configured_release=True)
            old_record_path = old_cache_path.with_name(name + '.record.json')
            old, new = json.loads(old_record_path.read_text()), json.loads(obj.with_suffix('.record.json').read_text())
            result = {'before_cache_sha256': run.sha(old_cache_path),
                'before_record_sha256': run.sha(old_record_path),
                'before_object_sha256': old['sha256'], 'after_object_sha256': new['sha256'],
                'before_cache': str(old_cache_path), 'after_cache': str(obj.with_suffix('.compile.json')),
                'command': new_cache['command']}
            for kind in ('functions', 'constants', 'unwind'):
                names = sorted(set(old[kind]) | set(new[kind]))
                result[kind] = {'same': [n for n in names if old[kind].get(n) == new[kind].get(n)],
                    'changed': [n for n in names if old[kind].get(n) != new[kind].get(n)]}
            projects = {}
            result['dependency_digests'] = {}
            old_source = Path(old_cache['command'][old_cache['command'].index('-c') + 1])
            # Earlier objects were compiled through the macOS mount alias;
            # this runner executes through Linux's canonical checkout path.
            old_root = Path(str(old_source).removesuffix(source).rstrip('/'))
            for arm, cache, root in (('before', old_cache, old_root), ('after', new_cache, tree)):
                projects[arm], system = {}, {}
                for p, h in cache['dependencies'].items():
                    if Path(p).is_relative_to(root):
                        projects[arm][str(Path(p).relative_to(root))] = h
                    else:
                        system[p] = h
                result['dependency_digests'][arm] = {kind: {'count': len(values),
                    'sha256': hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()}
                    for kind, values in (('project', projects[arm]), ('system', system))}
            assert all(manifest['before_sha256'].get(p) == h for p, h in projects['before'].items())
            result['source_path_correspondence'] = {str(old_root): str(baseline)}
            result['changed_project_dependencies'] = {p: [projects['before'].get(p), projects['after'].get(p)]
                for p in sorted(set(projects['before']) | set(projects['after']))
                if projects['before'].get(p) != projects['after'].get(p)}
            assert list(result['changed_project_dependencies']) == [HEADER]
            if name == 'producer':
                result['census'] = {'before': census(old), 'after': census(new)}
                assert result['census']['before']['predicate']['size'] == 24
                assert result['census']['after']['predicate']['size'] == 16
                assert result['census']['after']['checked_filter']['size'] == 24
            results[name] = result
            receipt['profiles'][profile] = results
            run.save(receipt_path, receipt)
            print(profile, name, {k: (len(result[k]['same']), len(result[k]['changed']))
                                  for k in ('functions', 'constants', 'unwind')}, flush=True)
    analyse(output, args.baseline_objects.resolve())


if __name__ == '__main__':
    main()
