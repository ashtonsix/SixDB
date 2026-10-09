#!/usr/bin/env python3
"""Target-compiled metadata facts and ordinary-ABI callers; no timing."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess


def run(argv):
    return subprocess.check_output(argv, text=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    study = Path(__file__).resolve().parent
    repo = study.parents[4]
    helper_path = repo / 'workbench/spikes/call-boundaries/access-interleaving/codegen.py'
    spec = importlib.util.spec_from_file_location('loom_access_codegen', helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.relevant = lambda name: name.startswith(('loom_metadata::', 'object_access_check::',
                                                   'loom::detail::MemoryRing<'))
    helper.role = lambda name: ('ordinary_packet_probe' if name.startswith('loom_metadata::') else
        'native_callback' if name.startswith('object_access_check::native(') else
        'ordinary_entry_bridge' if '::enter(' in name else
        'recurrent_yield' if '::yield(' in name else 'actual_caller_or_ring_support')
    compiler = run(['clang++-21', '--version'])
    if not re.search(r'\b21\.1\.8\b', compiler):
        raise ValueError('Pinned Clang21.1.8 required')
    output.mkdir(parents=True, exist_ok=False)
    caller = source / 'loom/test/object_access.cpp'
    baseline = source / 'workbench/spikes/orbital-mvcc/baseline/include'
    if not caller.exists():
        caller = source / 'workbench/spikes/orbital-mvcc/callers/loom.cpp'
    common = ['-std=c++23', '-O3', '-g0', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
        '-fno-fast-math', '-ffp-contract=off', '-fvisibility=hidden', '-fvisibility-inlines-hidden',
        '-ffunction-sections', '-fdata-sections', '-I' + str(study), '-I' + str(baseline),
        '-I' + str(source / 'loom/include'), '-I' + str(source / 'orbital/include'),
        '-I' + str(source / 'ikea/include'), '-DLM_CALLER_SOURCE="' + str(caller) + '"']
    cross = ['--target=x86_64-linux-gnu', '--sysroot=/usr/x86_64-linux-gnu',
        '-isystem', '/usr/x86_64-linux-gnu/include/c++/15', '-isystem',
        '/usr/x86_64-linux-gnu/include/c++/15/x86_64-linux-gnu']
    targets = {'v2': ('arm', ['-mcpu=neoverse-v2']),
               'zen5': ('x86', cross + ['-march=znver5'])}
    receipt = {'compiler': compiler, 'source_root': str(source),
        'probe_sha256': {p.name: sha(p) for p in study.iterdir() if p.suffix in ('.cpp', '.h', '.py')},
        'helper_sha256': sha(helper_path), 'targets': {}, 'native_checks': [],
        'limits': ['Static sites and ABI lowering, not executed instructions, latency or throughput.',
                   helper.STACK_NOTE, 'Independent packet producer and consumer TUs; no LTO.',
                   'The facts TU includes the real frozen caller to inspect its types and emitted bodies.',
                   '8/16/32-byte packets and Result16 are mechanical controls, not MVCC contracts.',
                   'Cross target is compile-only; installed native/cross headers are independently hashed.']}
    for target, (arch, flags) in targets.items():
        directory = output / target
        directory.mkdir()
        entries, deps = [], {}
        for name in ('boundaries', 'callers', 'facts'):
            cpp, obj = study / (name + '.cpp'), directory / (name + '.o')
            depfile = obj.with_suffix('.d')
            command = ['clang++-21', *common, *flags, '-MD', '-MF', str(depfile),
                       '-c', str(cpp), '-o', str(obj)]
            layout_file = None
            if name == 'facts':
                command[1:1] = ['-Xclang', '-fdump-record-layouts']
                layout_file = directory / 'record-layouts.log'
                compiled = subprocess.run(command, capture_output=True, text=True)
                layout_file.write_text(compiled.stdout)
                if compiled.returncode:
                    raise RuntimeError(compiled.stderr)
            else:
                subprocess.run(command, check=True)
            current = {str(p): sha(p) for p in helper.dependencies(depfile, repo)}
            deps.update(current)
            ir = obj.with_suffix('.ll')
            ir_command = ['clang++-21', *common, *flags, '-S', '-emit-llvm', str(cpp), '-o', str(ir)]
            subprocess.run(ir_command, check=True)
            text = ir.read_text()
            entry = {'source': str(cpp), 'argv': command, 'object_sha256': sha(obj),
                'ir_argv': ir_command, 'ir_sha256': sha(ir),
                'signatures': [line for line in text.splitlines() if
                    line.startswith(('define ', 'declare ')) and
                    any(x in line for x in ('loom_metadata', 'object6select', 'object6retain'))],
                **helper.inspect(obj, arch)}
            if name == 'facts':
                entry['facts'] = {match[1]: list(map(int, re.findall(r'i64 (\d+)', match[2])))
                    for match in re.finditer(r'^@lm_fact_(\w+).*constant \[6 x i64\] \[([^\]]+)\]',
                                             text, re.MULTILINE)}
                if len(entry['facts']) < 60:
                    raise ValueError('Facts parser missed target constants')
                layouts = layout_file.read_text().split('*** Dumping AST Record Layout')
                selected = [record for record in layouts if re.search(
                    r'^\s*0 \| (?:struct|class) (?:object_access_check::|Caller|\(lambda at .*object_access)',
                    record, re.MULTILINE)]
                entry['caller_record_layouts'] = [record[:match.end()].strip() for record in selected
                    if (match := re.search(r'\[sizeof=.*?\]', record, re.DOTALL))]
                entry['record_layouts_sha256'] = sha(layout_file)
            entries.append(entry)
        receipt['targets'][target] = {'objects': entries, 'dependency_sha256': deps}
        print(f'{target}: compiled actual layouts, boundaries and callers', flush=True)
    # A separate native TU checks full generation/error preservation and packet
    # round-trips. It makes no Object/physical-runtime simulation.
    check = output / 'packet-check'
    command = ['clang++-21', *common, str(study / 'check.cpp'),
        str(output / 'v2/boundaries.o'), '-Wl,--gc-sections', '-o', str(check)]
    subprocess.run(command, check=True)
    checked = run([str(check)])
    (output / 'native-check.log').write_text(checked)
    receipt['native_checks'].append({'argv': command, 'binary_sha256': sha(check),
        'run_argv': [str(check)], 'output': checked})
    for target in receipt['targets'].values():
        for path, digest in target['dependency_sha256'].items():
            if sha(Path(path)) != digest:
                raise ValueError('Dependency changed during qualification: ' + path)
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Native packet controls passed; full dependency receipts unchanged', flush=True)


if __name__ == '__main__':
    main()
