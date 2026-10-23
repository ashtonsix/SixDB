#!/usr/bin/env python3
"""Source-verified, bounded native ALP/RD correctness audit. No timings/downloads."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[5]
HERE = Path(__file__).resolve().parent
REVISION = '31ca0ed11c93c99d3f5b5c30e01a3e1c3832d3ce'
ARCHIVE_SHA = '080807a58508ad4041f00251c14feae3a984d7630046f55aa42b375fb491b6f3'
CORPUS = '85f48a79cec7ed1b4048004612552cdfc950adca7498186e268cf4ec66eb2bfc'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def digest(mapping):
    return hashlib.sha256(json.dumps(mapping, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def verified_source(source):
    archive = source.parent / (source.name + '.tar.gz')
    assert sha(archive) == ARCHIVE_SHA
    expected = {}
    with tarfile.open(archive) as stream:
        for item in stream.getmembers():
            if item.isfile():
                name = str(Path(*Path(item.name).parts[1:]))
                expected[name] = hashlib.sha256(stream.extractfile(item).read()).hexdigest()
    assert all(sha(source / name) == value for name, value in expected.items())
    return expected


def classify(result, rows):
    if 'runtime error: index ' in result.stderr and 'out of bounds' in result.stderr:
        return 'ubsan_bounds'
    if 'runtime error:' in result.stderr and 'outside the range of representable values' in result.stderr:
        return 'ubsan_float_cast'
    if 'runtime error: signed integer overflow' in result.stderr:
        return 'ubsan_signed_overflow'
    if result.returncode == 0 and not result.stderr and rows and rows[-1].get('mismatches') == 0:
        return 'bit_exact'
    if result.returncode == 1 and not result.stderr and rows and rows[-1].get('mismatches', 0) > 0:
        return 'ieee_mismatch'
    return 'unexpected'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/frame-helper/alp-audit/sanitize')
    parser.add_argument('--source', type=Path, default=ROOT / f'build/peers/frame/alp-{REVISION}')
    parser.add_argument('--corpus', type=Path, default=ROOT / f'build/datasets/prepared/{CORPUS}')
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'aarch64', 'Native NEON audit; use orb -m ubuntu on Mac'
    source, build, corpus = args.source.resolve(), args.build.resolve(), args.corpus.resolve()
    upstream = verified_source(source)
    own = {p.name: sha(p) for p in (HERE / 'CMakeLists.txt', HERE / 'check.cpp', HERE / 'run.py')}
    corpus_inputs = {p.name: sha(p) for p in sorted(corpus.glob('*.f64'))}
    assert len(corpus_inputs) == 16
    for name in corpus_inputs:
        assert (corpus / name).stat().st_size == 65536 * 8
    corpus_inputs['columns.json'] = sha(corpus / 'columns.json')
    build.mkdir(parents=True, exist_ok=True)
    output = build.parent / 'evidence'
    output.mkdir(exist_ok=True)
    commands = [
        ['cmake', '-S', str(HERE), '-B', str(build), '-G', 'Ninja',
         f'-DALP_AUDIT_SOURCE={source}', '-DALP_AUDIT_SANITIZE=ON'],
        ['taskset', '-c', '8', 'cmake', '--build', str(build), '-j1']]
    for index, command in enumerate(commands):
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        (output / f'build-{index}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr
    binary = build / 'frame_alp_audit'
    cases = []
    for typ in ['float', 'double']:
        for dictionary in range(1, 9):
            for population in range(4):
                cases.append(('rd', [typ, str(dictionary), str(population)], {'bit_exact'}))
        cases.append(('rd', [typ, '8', '4'], {'bit_exact'}))
        for rounding in range(4):
            for flush in range(2):
                cases.append(('rd-special', [typ, str(rounding), str(flush)], {'bit_exact'}))
        cases.append(('rd-raw', [typ, '8', '3'], {'ubsan_bounds'}))
    for path in sorted(corpus.glob('*.f64')):
        cases.append(('rd-corpus', [str(path)], {'bit_exact'}))
        cases.append(('alp-corpus', [str(path)], {'bit_exact', 'ubsan_signed_overflow', 'ubsan_float_cast'}))
    cases.append(('alp', ['float', 'plain', '0', '0', '0'], {'ubsan_bounds'}))
    for shape in ['zero', 'infinity', 'nan', 'large', 'mostly_special']:
        cases.append(('alp', ['double', shape, '0', '0', '0'], {'ubsan_float_cast'}))
    for shape in ['train-zero', 'train-infinity', 'train-nan', 'train-large']:
        cases.append(('alp', ['double', shape, '0', '0', '0'], {'ubsan_signed_overflow'}))
    for encoding in range(4):
        for decoding in range(4):
            cases.append(('alp', ['double', 'plain', str(encoding), str(decoding), '0'], {'bit_exact', 'ieee_mismatch'}))
    cases.append(('alp', ['double', 'denorm', '0', '0', '0'], {'bit_exact'}))
    cases.append(('alp', ['double', 'denorm', '0', '0', '1'], {'ieee_mismatch'}))
    results = []
    for index, (mode, arguments, expected) in enumerate(cases):
        argv = ['taskset', '-c', '8', str(binary), mode, *arguments]
        result = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True)
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        classification = classify(result, rows)
        log = output / f'case-{index:03d}.log'
        log.write_text(result.stdout + result.stderr)
        results.append(dict(argv=argv, returncode=result.returncode, classification=classification,
            expected=sorted(expected), observations=rows, log=log.name, log_sha256=sha(log),
            diagnostic=result.stderr.splitlines()[0] if result.stderr else None))
        if classification not in expected:
            print(result.stdout, result.stderr)
            raise AssertionError((mode, arguments, classification))
    assert verified_source(source) == upstream
    assert own == {name: sha(HERE / name) for name in own}
    assert corpus_inputs == {name: sha(corpus / name) for name in corpus_inputs}
    compiles = json.loads((build / 'compile_commands.json').read_text())
    relevant = {name: value for name, value in upstream.items()
                if name.startswith('include/alp/') or name in {
                    'include/alp.hpp', 'include/fastlanes/ffor.hpp', 'include/fastlanes/unffor.hpp',
                    'src/fastlanes_generated_ffor.cpp', 'src/fastlanes_generated_unffor.cpp',
                    'src/fastlanes_ffor.cpp', 'src/fastlanes_unffor.cpp'}}
    receipt = dict(revision=REVISION, archive_sha256=ARCHIVE_SHA,
        upstream_files=len(upstream), upstream_snapshot_sha256=digest(upstream),
        upstream_consumed_region_sha256=relevant, owned_inputs_sha256=own,
        inputs_unchanged=True, corpus_id=CORPUS, corpus_inputs_sha256=corpus_inputs,
        compiler=subprocess.check_output(['clang++-21', '--version'], text=True).strip(),
        host=subprocess.check_output(['uname', '-a'], text=True).strip(),
        build_commands=commands, compile_commands=compiles, binary_sha256=sha(binary),
        classifications=dict(Counter(r['classification'] for r in results)), results=results,
        all_expected_observations=True,
        upstream_qualified_for_arbitrary_IEEE_ALP=False,
        scope='Executed native NEON, strict dynamic FP, ASan/UBSan/float-cast-overflow witness and all four linked upstream FFOR/unFFOR TUs. No fused falp, whole upstream build, foreign ISA, performance, file-format, short-vector or corrupt-input qualification.')
    destination = output / 'runs.json'
    destination.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(cases=len(results), classifications=receipt['classifications'],
        all_expected_observations=True, upstream_qualified_for_arbitrary_IEEE_ALP=False,
        receipt=str(destination))))


if __name__ == '__main__':
    main()
