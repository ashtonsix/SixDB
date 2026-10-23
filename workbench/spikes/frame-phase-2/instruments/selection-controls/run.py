#!/usr/bin/env python3
"""Build and qualify actual pinned native peer controls; no timing or workers."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shlex
import struct
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[5]
HERE = Path(__file__).resolve().parent
PROVIDER = ROOT / 'workbench/peers/frame-selection.cmake'
SAMPLE = '536c2f7c742270d974839b658fa20ef0c88342666d8dac5b7dc3021f6fddeaef'
HIGHWAY = '2607d3b5b0113992fe84d3848859eae13b3b52c1'
HASHES = dict(roaring_c='81ae778a927108500a7d577ae3ff44fa1dfa5c46dfe0f0d0bedd8e4ae624efe2',
    roaring_h='5a09d9e2996b8c0c545b8147996a3b008bcad1a991c559c9b4ecac3120a2b468',
    highway='741d705781e0b3e406beda8f1f994fbae01321237ce8023a1ad90fbaf7940c25')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def sources(build):
    deps = build / '_deps'
    roaring = deps / 'frame-selection/croaring-v5.3.2'
    assert sha(roaring / 'roaring.c') == HASHES['roaring_c']
    assert sha(roaring / 'roaring.h') == HASHES['roaring_h']
    # FetchContent verifies the archive at acquisition; compare all extracted
    # upstream files with the same retained source archive when available.
    downloads = deps / 'frame_selection_highway-subbuild/frame_selection_highway-populate-prefix/src'
    archive = downloads / 'archive.tar'
    assert sha(archive) == HASHES['highway']
    expected = {}
    with tarfile.open(archive) as stream:
        for member in stream.getmembers():
            if member.isfile():
                name = str(Path(*Path(member.name).parts[1:]))
                expected[name] = hashlib.sha256(stream.extractfile(member).read()).hexdigest()
    source = deps / 'frame_selection_highway-src'
    assert all(sha(source / name) == value for name, value in expected.items())
    return dict(croaring=HASHES, highway_files=len(expected), highway_snapshot_sha256=digest(expected))


def inspect_dispatch(binary, observed, output):
    plain = subprocess.check_output(['llvm-nm-21', '-S', '--defined-only', '--format=posix', str(binary)], text=True)
    symbols = {}
    for line in plain.splitlines():
        fields = line.split()
        if len(fields) == 4:
            symbols[fields[0]] = dict(address=int(fields[2], 16), bytes=int(fields[3], 16))
    table_name = '_ZN3hwy12_GLOBAL__N_130SortU32AscHighwayDispatchTableE'
    wrapper_name = '_ZN3hwy6VQSortEPjmNS_13SortAscendingE'
    table, wrapper = symbols[table_name], symbols[wrapper_name]
    raw = binary.read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    size, count = struct.unpack_from('<HH', raw, 54)
    index = observed['selected_index']
    assert index * 8 < table['bytes']
    slot = table['address'] + index * 8
    offset = None
    for i in range(count):
        typ, _, off, va, _, length, _, _ = struct.unpack_from('<IIQQQQQQ', raw, phoff + i * size)
        if typ == 1 and va <= slot < va + length:
            offset = off + slot - va
    assert offset is not None
    target = struct.unpack_from('<Q', raw, offset)[0]
    demangled = subprocess.check_output(['llvm-nm-21', '-S', '--defined-only', '--demangle', str(binary)], text=True)
    resolved = [line for line in demangled.splitlines() if line.startswith(f'{target:016x} ')]
    assert len(resolved) == 1 and f'hwy::N_{observed["selected_target"]}::' in resolved[0] and 'SortU32Asc(' in resolved[0]
    compiled = [line for line in demangled.splitlines() if '::(anonymous namespace)::SortU32Asc(' in line]
    command = ['llvm-objdump-21', '-d', '--demangle', f'--start-address={wrapper["address"]}',
               f'--stop-address={wrapper["address"] + wrapper["bytes"]}', str(binary)]
    assembly = subprocess.check_output(command, text=True)
    destination = output / 'vqsort-dispatch.txt'
    destination.write_text(assembly)
    assert 'br\t' in assembly or 'br ' in assembly
    return dict(observed_index=index, table_name=table_name, table=table, slot=slot,
        resolved_target=target, resolved_symbol=resolved[0], compiled_u32_targets=compiled,
        wrapper_name=wrapper_name, wrapper=wrapper, objdump_argv=command,
        disassembly_sha256=sha(destination), binary_sha256=sha(binary))


def compatibility_gate(compiles, output, profile):
    entry = next(c for c in compiles if c['file'].endswith('/vqsort_u32a.cc'))
    original = shlex.split(entry['command'])
    command = []
    index = 0
    while index < len(original):
        arg = original[index]
        if arg == '-o':
            index += 2
            continue
        if arg != '-c':
            command.append(arg)
        index += 1
    command += ['-E', '-dM']
    macros = subprocess.check_output(command, cwd=entry['directory'], text=True)
    chosen = [line for line in macros.splitlines() if line.startswith((
        '#define VQSORT_COMPILER_COMPATIBLE ', '#define HWY_IS_ASAN ', '#define HWY_IS_UBSAN '))]
    assert '#define VQSORT_COMPILER_COMPATIBLE ' + ('0' if profile == 'sanitize' else '1') in chosen
    log = output / f'{profile}-compatibility.txt'
    log.write_text('\n'.join(chosen) + '\n')
    return dict(argv=command, macros=chosen, output_sha256=sha(log),
        algorithm='upstream HeapSort fallback' if profile == 'sanitize' else 'vector vqsort enabled')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, default=ROOT / 'build/frame-helper/selection-controls')
    parser.add_argument('--windows', type=Path, default=ROOT / f'build/datasets/prepared/{SAMPLE}')
    parser.add_argument('--synthetic', action='store_true', help='Skip real windows, label reduced scope')
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'aarch64', 'Use the pinned native Linux NEON toolchain'
    root = args.build_root.resolve()
    output = root / 'evidence'
    output.mkdir(parents=True, exist_ok=True)
    inputs = {str(p.relative_to(ROOT)): sha(p) for p in [PROVIDER, HERE / 'CMakeLists.txt',
              HERE / 'roaring_check.cpp', HERE / 'vqsort_check.cpp', HERE / 'run.py']}
    corpus, files, windows, corpus_hashes = None, [], 0, {}
    if not args.synthetic:
        path = args.windows.resolve()
        sys.path.insert(0, str(ROOT / 'workbench/tools'))
        import datasets
        meta = datasets.verify(path)
        files = sorted(path.glob('*.windows'))
        assert 0 < len(files) <= 12
        lineage = json.loads((path / 'lineage.json').read_text())
        for file in files:
            wire = file.read_bytes()
            rows = lineage[file.stem]
            assert len(wire) == len(rows) * 8192 and 0 < len(rows) <= 8
            for i, row in enumerate(rows):
                assert sum(b.bit_count() for b in wire[i * 8192:(i + 1) * 8192]) == row['cardinality']
                assert 0 <= row['high16'] <= 65535 and len(row['bitmap_id']) == 64
            windows += len(rows)
            corpus_hashes[str(file)] = sha(file)
        corpus_hashes[str(path / 'lineage.json')] = sha(path / 'lineage.json')
        corpus_hashes[str(path / 'prepared.json')] = sha(path / 'prepared.json')
        corpus = dict(path=str(path), id=meta['id'], windows=windows,
            argv_window_order=[str(f) for f in files], lineage_sha256=sha(path / 'lineage.json'),
            semantics='Rebased low16 whole windows; all cells retained. Arbitrary correctness pairs, not a query mix or reconstructed full original bitmap.')
    results = []
    for profile in ['normal', 'ubsan', 'sanitize']:
        build = root / profile
        configure = ['cmake', '-S', str(HERE), '-B', str(build), '-G', 'Ninja',
            '-DSIXDB_FRAME_SELECTION_SANITIZE=' + ('OFF' if profile == 'normal' else 'ON'),
            '-DSIXDB_FRAME_SELECTION_SANITIZERS=' + ('undefined' if profile == 'ubsan' else 'address,undefined')]
        commands = [configure, ['taskset', '-c', '8', 'cmake', '--build', str(build),
                    '--target', 'frame_roaring_check', 'frame_vqsort_check', '-j1']]
        for i, command in enumerate(commands):
            run = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
            (output / f'{profile}-build-{i}.log').write_text(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
        before = sources(build)
        peer_results = {}
        for peer in ['roaring', 'vqsort']:
            binary = build / f'frame_{peer}_check'
            command = ['taskset', '-c', '8', str(binary), *([str(f) for f in files] if peer == 'roaring' else [])]
            if peer == 'vqsort' and profile == 'sanitize':
                command.append('--nonempty')
            run = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
            log = output / f'{profile}-{peer}.log'
            log.write_text(run.stdout + run.stderr)
            assert run.returncode == 0 and not run.stderr, log
            records = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
            assert records[-1]['pass'] and not records[-1]['timing_claim']
            if peer == 'roaring':
                assert records[-1]['images'] == 12 + windows
                assert records[-1]['Boolean_operations'] == 1728 + 24 * windows
                assert records[0]['cardinality'] == 1 << 32
            else:
                assert records[-1]['integer_sorts'] == (1764 if profile == 'sanitize' else 1848)
                assert records[-1]['payload_sorts'] == (504 if profile == 'sanitize' else 528)
            peer_results[peer] = dict(argv=command, returncode=0, binary_sha256=sha(binary),
                output_sha256=sha(log), records=records)
            print(json.dumps(dict(profile=profile, peer=peer, result=records[-1])), flush=True)
        assert sources(build) == before
        compiles = json.loads((build / 'compile_commands.json').read_text())
        results.append(dict(profile=profile, source_verification=before, build_commands=commands,
            compile_commands=compiles, compatibility=compatibility_gate(compiles, output, profile), peers=peer_results))
    assert inputs == {name: sha(ROOT / name) for name in inputs}
    assert corpus_hashes == {name: sha(Path(name)) for name in corpus_hashes}
    # The AArch64 ASan profile deliberately substitutes upstream HeapSort.
    # Its zero-size loop underflow is a dedicated bounded negative, not a
    # reason to silently exclude empty inputs from the optimized control.
    command = ['taskset', '-c', '8', str(root / 'sanitize/frame_vqsort_check'), '--zero']
    process = subprocess.Popen(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = process.communicate(timeout=2)
        raise AssertionError('Pinned ASan empty fallback no longer reproduces: inspect and update the audit')
    except subprocess.TimeoutExpired:
        process.terminate()
        stdout, stderr = process.communicate(timeout=10)
    assert 'entered direct VQSort' in stdout and 'returned direct VQSort' not in stdout and not stderr
    negative_log = output / 'asan-zero-negative.log'
    negative_log.write_text(stdout + stderr)
    negative = dict(argv=command, returncode=process.returncode, owner_terminated=True,
        classification='AArch64 ASan fallback zero-size build-heap counter underflow',
        bounded_guard_seconds=2, output_sha256=sha(negative_log),
        note='Source and debugger independently establish num_lanes=0 and huge unsigned counter; the two-second guard is resource control, not a latency result.')
    normal = results[0]['peers']['vqsort']
    dispatch = inspect_dispatch(root / 'normal/frame_vqsort_check', normal['records'][-1], output)
    receipt = dict(owned_inputs_sha256=inputs, inputs_unchanged=True, corpus=corpus,
        corpus_inputs_sha256=corpus_hashes, compiler=subprocess.check_output(['clang++-21', '--version'], text=True),
        host=subprocess.check_output(['uname', '-a'], text=True), profiles=results, dispatch=dispatch, zero_negative=negative,
        scope='Native Linux NEON optimized and UBSan vector vqsort; default AArch64 ASan/UBSan uses upstream HeapSort fallback, nonempty checks plus dedicated zero negative. CRoaring has full ASan/UBSan coverage. All compiled peer TUs instrumented in sanitizer profiles. No speed, native x86, float ordering, stability or Frame policy claim.')
    (output / 'runs.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(receipt=str(output / 'runs.json'), pass_checks=True, timing_claim=False)))


if __name__ == '__main__':
    main()
