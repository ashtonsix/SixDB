#!/usr/bin/env python3
"""Compare complete Directory objects while keeping its parent modules fixed.

The two supplied source roots are frozen cohesion captures. Engine, Orbital and
Loom are taken from the baseline root for BOTH arms and verified against Git.
Only Ikea changes. Commands retain the configured Directory source flags, with
explicit pinned cross-target ISA/tuning substitutions. ELF instructions, data,
unwind records and relocation targets/addends are compared without type renames.
This does not measure linked placement or runtime performance.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BASELINE = 'f153420002520213e1496a97559f0118bbd1ad94'
SOURCES = ('engine/src/directory.cpp', 'engine/src/directory_material.cpp')
CONTROLS = ('engine', 'orbital', 'loom')
SPEC = importlib.util.spec_from_file_location('cohesion_codegen', Path(__file__).with_name('cohesion-codegen.py'))
base = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(base)
base.normalize = lambda name, source: name


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def object_record(obj, source):
    # Complete/base constructors can alias one body but have distinct mangled
    # symbols. Keep that identity on functions AND relocation targets instead
    # of silently merging two equally demangled declarations.
    original_run = base.run

    def named(command, **kwargs):
        output = original_run(command, **kwargs)
        if command == ['llvm-cxxfilt-21']:
            inputs = kwargs['input'].splitlines()
            output = '\n'.join(f'{pretty} [symbol:{raw}]' if pretty != raw else raw
                               for raw, pretty in zip(inputs, output.splitlines(), strict=True)) + '\n'
        return output

    base.run = named
    try:
        return base.object_record(obj, source, 'llvm-cxxfilt-21')
    finally:
        base.run = original_run


def section_hashes(obj):
    data = obj.read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + i * header[11])
                for i in range(header[12])]
    strings = sections[header[13]]
    names = data[strings[4]:strings[4] + strings[5]]
    result = {}
    occurrences = {}
    for section in sections:
        name = names[section[0]:names.find(b'\0', section[0])].decode()
        occurrence = occurrences.get(name, 0)
        occurrences[name] = occurrence + 1
        key = name if occurrence == 0 else f'{name} [occurrence:{occurrence}]'
        body = data[section[4]:section[4] + section[5]] if section[1] != 8 else b''
        result[key] = [section[1], section[2], section[6], section[7], section[8], section[9], section[5],
                        hashlib.sha256(body).hexdigest()]
    return result


def verified_controls(before):
    rows = subprocess.check_output(['git', 'ls-tree', '-rz', BASELINE, *CONTROLS], cwd=ROOT).split(b'\0')
    entries = []
    for row in filter(None, rows):
        header, name = row.split(b'\t', 1)
        mode, kind, oid = header.decode().split()
        assert kind == 'blob' and mode in ('100644', '100755'), row
        entries.append((name.decode(), oid))
    data = subprocess.run(['git', 'cat-file', '--batch'], cwd=ROOT, check=True,
                          input=''.join(oid + '\n' for _, oid in entries).encode(),
                          stdout=subprocess.PIPE).stdout
    hashes, offset = {}, 0
    for name, oid in entries:
        end = data.index(b'\n', offset)
        actual, kind, length = data[offset:end].decode().split()
        assert actual == oid and kind == 'blob'
        offset = end + 1
        body = data[offset:offset + int(length)]
        offset += int(length) + 1
        assert (before / name).read_bytes() == body, name
        hashes[name] = hashlib.sha256(body).hexdigest()
    assert offset == len(data)
    return hashes


def configured_command(row, tree, profile, output):
    target, march, tune, macro = base.PROFILES[profile]
    configured = row.get('arguments') or shlex.split(row['command'])
    source = next(s for s in SOURCES if row['file'].endswith('/' + s))
    original_root = Path(row['file']).parents[2]
    result = ['clang++-21', '--target=' + target, '--gcc-toolchain=/usr']
    i = 1
    while i < len(configured):
        arg = configured[i]
        i += 1
        if arg in ('-o', '-c'):
            i += 1
            continue
        if arg.startswith(('-DSIXDB_TUNE_', '-march=', '-mtune=', '-ffile-prefix-map=')):
            continue
        result.append(arg.replace(str(original_root), str(tree)))
    result += [f'-march={march}', f'-mtune={tune}',
               *[f'-DSIXDB_TUNE_{t}={int(t == macro)}' for t in base.TUNES],
               f'-ffile-prefix-map={tree}=/sixdb', '-MD', '-MF', str(output.with_suffix('.d')),
               '-c', str(tree / source), '-o', str(output)]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--configured-commands', type=Path, required=True)
    parser.add_argument('--reuse', action='store_true', help='reuse only command/object/dependency-verified work')
    parser.add_argument('--records-only', action='store_true',
                        help='reparse verified existing objects; refuse any compilation')
    args = parser.parse_args()
    assert not args.records_only or args.reuse, '--records-only requires --reuse'
    before, after, out = args.before.resolve(), args.after.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    assert args.reuse or not (out / 'sources').exists(), 'choose fresh output; sources must remain frozen'
    controls = verified_controls(before)
    commands = json.loads(args.configured_commands.read_text())
    rows = {s: next(row for row in commands if row['file'].endswith('/' + s)) for s in SOURCES}
    receipt = {'scope': __doc__, 'baseline_commit': BASELINE,
               'runner_sha256': digest(Path(__file__)),
               'elf_parser_sha256': digest(Path(base.__file__)),
               'configured_commands_sha256': digest(args.configured_commands),
               'configured_source_commands': rows, 'control_sha256': controls,
               'compiler': base.run(['clang++-21', '--version']), 'compiles': []}
    trees = {}
    for arm, original in [('before', before), ('after', after)]:
        tree = out / 'sources' / arm
        trees[arm] = tree
        for module in (*CONTROLS, 'ikea'):
            wanted = (original if module == 'ikea' else before) / module
            destination = tree / module
            if destination.exists():
                assert args.reuse
                wanted_hashes = {str(p.relative_to(wanted)): digest(p) for p in wanted.rglob('*') if p.is_file()}
                actual_hashes = {str(p.relative_to(destination)): digest(p) for p in destination.rglob('*') if p.is_file()}
                assert wanted_hashes == actual_hashes, 'frozen source changed: ' + str(destination)
            else:
                shutil.copytree(wanted, destination)
    deps = {}
    comparisons = []
    for profile in base.PROFILES:
        for source in SOURCES:
            records = {}
            for arm in ('before', 'after'):
                directory = out / profile / arm
                directory.mkdir(parents=True, exist_ok=True)
                obj = directory / (Path(source).stem + '.o')
                cmd = configured_command(rows[source], trees[arm], profile, obj)
                cached = obj.with_suffix('.compile.json')
                cached_row = json.loads(cached.read_text()) if cached.exists() else None
                reusable = (args.reuse and cached_row and cached_row['command'] == cmd and
                            obj.exists() and digest(obj) == cached_row['object_sha256'] and
                            all(Path(p).exists() and digest(Path(p)) == h
                                for p, h in cached_row['dependencies'].items()))
                print('compile', profile, source, arm, flush=True)
                if not reusable:
                    assert not args.records_only, 'unverified object; compilation refused: ' + str(obj)
                    with obj.with_suffix('.log').open('w') as log:
                        subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                obj.with_suffix('.s').write_text(base.run(['llvm-objdump-21', '-d', '-r', '--demangle', str(obj)]))
                obj.with_suffix('.sections').write_text(base.run(['llvm-objdump-21', '-s', str(obj)]))
                record = object_record(obj, source)
                save(obj.with_suffix('.record.json'), record)
                records[arm] = record
                paths = shlex.split(obj.with_suffix('.d').read_text().replace('\\\n', ' ').split(':', 1)[1])
                hashes = {p: digest(Path(p)) for p in paths}
                deps.update(hashes)
                compiled = {'profile': profile, 'source': source, 'arm': arm,
                            'command': cmd, 'object_sha256': digest(obj), 'dependencies': hashes}
                save(cached, compiled)
                receipt['compiles'].append(compiled)
                save(out / 'receipt.json', receipt)
            comparison = {'profile': profile, 'source': source}
            for kind in ('functions', 'constants', 'unwind'):
                entries = base.compare(records['before'][kind], records['after'][kind])
                comparison[kind] = {'total': len(entries), 'equal': sum(e['equal'] for e in entries),
                                    'differences': [e for e in entries if not e['equal']]}
            sections = {arm: section_hashes(out / profile / arm / (Path(source).stem + '.o'))
                        for arm in ('before', 'after')}
            changed = sorted(name for name in sections['before'].keys() | sections['after'].keys()
                             if sections['before'].get(name) != sections['after'].get(name))
            comparison['raw_changed_sections'] = changed
            comparison['all_non_debug_sections_exact'] = all('debug' in name for name in changed)
            comparisons.append(comparison)
            save(out / 'comparisons.json', comparisons)
    system = {p: h for p, h in deps.items() if not p.startswith(str(out / 'sources') + '/')}
    project = {p: h for p, h in deps.items() if p not in system}
    aggregate = lambda values: hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()
    summary = {'baseline_commit': BASELINE, 'scope': __doc__,
               'runner_sha256': receipt['runner_sha256'],
               'elf_parser_sha256': receipt['elf_parser_sha256'],
               'symbol_identity': 'Complete/base constructor aliases retain their distinct raw mangled identities in function and relocation labels; counts include those function symbols.',
               'control_manifest_sha256': aggregate(controls), 'control_files': len(controls),
               'configured_commands_sha256': receipt['configured_commands_sha256'],
               'compiler': receipt['compiler'],
               'objects': [{k: v for k, v in row.items() if k != 'dependencies'} for row in receipt['compiles']],
               'project_dependencies': {'count': len(project), 'sha256': aggregate(project)},
               'system_dependencies': {'count': len(system), 'sha256': aggregate(system)},
               'comparisons': comparisons}
    save(out / 'summary.json', summary)
    print(json.dumps({'summary': str(out / 'summary.json'), 'comparisons': comparisons}, indent=2))


if __name__ == '__main__':
    main()
