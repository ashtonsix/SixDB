#!/usr/bin/env python3
"""Six bounded resident system cells using existing pinned local dependencies."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import signal
import shutil
import statistics
import struct
import subprocess
import tarfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
NAMES = ('OS', 'URLHash', 'EventTime', 'WatchID', 'ClientIP')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cached_source_snapshots(polars):
    """Verify pinned cached archives/trees without acquisition or extraction."""
    result = {}
    for label, revision, repo, expected in polars.SOURCES:
        archive = polars.TASK / f'{label}-{revision}.tar.gz'
        if not archive.is_file():
            raise RuntimeError(f'Missing cached source archive: {archive}; no download permitted')
        assert sha(archive) == expected, archive
        with tarfile.open(archive) as stream:
            members = stream.getmembers()
            prefix = members[0].name.split('/')[0]
            destination = polars.TASK / 'sources' / prefix
            if not destination.is_dir():
                raise RuntimeError(f'Missing cached source tree: {destination}; no extraction permitted')
            hashes = {}
            for member in members:
                if member.isfile():
                    name = str(Path(*Path(member.name).parts[1:]))
                    hashes[name] = hashlib.sha256(stream.extractfile(member).read()).hexdigest()
                    assert sha(destination / name) == hashes[name], name
        snapshot = hashlib.sha256(json.dumps(hashes, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        result[label] = dict(revision=revision, url=f'https://codeload.github.com/{repo}/tar.gz/{revision}',
                            archive_sha256=expected, source_files=len(hashes), snapshot_sha256=snapshot)
    return result


def load_snapshot(cpu):
    """Record admission evidence, not a claim about subsequent cache residence."""
    compilers = []
    compiler_names = {'clang', 'clang++', 'cc1', 'cc1plus', 'rustc', 'gcc', 'g++'}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            command = (entry / 'cmdline').read_bytes().split(b'\0')
            executable = Path(os.fsdecode(command[0])).name
            stem = executable.split('-')[0]
            if stem in compiler_names:
                compilers.append(dict(pid=int(entry.name), argv=[os.fsdecode(x) for x in command if x]))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    line = next(line for line in Path('/proc/stat').read_text().splitlines() if line.startswith(f'cpu{cpu} '))
    # Linux guest ticks are already included in user/nice, so exclude fields 8+.
    ticks = [int(field) for field in line.split()[1:9]]
    return dict(monotonic_ns=time.monotonic_ns(), active_compilers=compilers,
                loadavg=Path('/proc/loadavg').read_text().strip(), cpu=cpu, ticks=ticks)


def admit_timing(cpu):
    before = load_snapshot(cpu)
    time.sleep(0.3)
    after = load_snapshot(cpu)
    delta = [b - a for a, b in zip(before['ticks'], after['ticks'])]
    idle_fraction = (delta[3] + delta[4]) / sum(delta) if sum(delta) else 0
    return dict(before=before, after=after, selected_cpu_idle_fraction=idle_fraction,
                accepted=not (before['active_compilers'] or after['active_compilers']) and idle_fraction >= 0.9)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--rows', type=int, default=65536)
    parser.add_argument('--rounds', type=int, default=9)
    parser.add_argument('--cpu', type=int, default=8)
    parser.add_argument('--prepare-only', action='store_true', help='Build, freeze exact input snapshots and qualify; do not start query timers')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/frame-helper/system-corpus-v1')
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'aarch64', 'Existing cached native control is AArch64'
    assert 0 < args.rows <= 65536 and args.rows % 256 == 0 and 0 < args.rounds <= 101 and args.rounds % 2 == 1
    assert args.cpu in os.sched_getaffinity(0), 'Requested CPU is not available to this process'
    args.inputs = args.inputs.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    old = ROOT / 'workbench/spikes/frame-phase-2/instruments/systems'
    duck = ROOT / 'build/clang/frame-systems-neon/_deps/workbench_duckdb-src'
    sources = [HERE / 'duckdb.cpp', HERE / 'run.py', HERE / 'polars/Cargo.toml', HERE / 'polars/Cargo.lock',
               HERE / 'polars/rust-toolchain.toml', HERE / 'polars/src/main.rs', old / 'polars/run.py']
    source_sha256 = {str(p.relative_to(ROOT)): sha(p) for p in sources}
    qualification = json.loads((old / 'qualification.json').read_text())
    assert sha(duck / 'libduckdb.so') == qualification['engine_library_sha256'], 'Missing or changed cached DuckDB; no download permitted'
    assert (duck / 'duckdb.h').is_file()
    spec = importlib.util.spec_from_file_location('qualified_polars', old / 'polars/run.py')
    polars = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(polars)
    env = polars.environment()
    # Run the actual pinned executables: a Rustup shim may install a missing toolchain.
    toolchain = polars.TASK / 'rustup/toolchains/nightly-2026-04-01-aarch64-unknown-linux-gnu/bin'
    cargo, rustc = toolchain / 'cargo', toolchain / 'rustc'
    assert cargo.is_file() and rustc.is_file(), 'Missing cached pinned toolchain; no bootstrap permitted'
    env['RUSTC'] = str(rustc)
    assert sha(HERE / 'polars/Cargo.lock') == sha(old / 'polars/Cargo.lock')
    upstream_before = cached_source_snapshots(polars)
    inputs = {}
    original = {}
    snapshots = output / 'inputs'
    snapshots.mkdir(exist_ok=True)
    input_reference = ROOT / 'workbench/spikes/frame-phase-1/encoding/worker-inputs.json'
    shutil.copyfile(input_reference, output / 'input-artifact.json')
    for name in NAMES:
        path = args.inputs / (name + '.u64')
        with path.open('rb') as stream:
            prefix = stream.read(args.rows * 8)
        assert len(prefix) == args.rows * 8
        snapshot = snapshots / (name + '.u64')
        snapshot.write_bytes(prefix)
        inputs[name] = dict(path=str(snapshot), snapshot_relative='inputs/' + snapshot.name,
                            original_path=str(path), original_file_sha256=sha(path),
                            file_sha256=sha(snapshot), prefix_sha256=hashlib.sha256(prefix).hexdigest(),
                            first_row=0, rows=args.rows, format='little-endian UInt64, original bits/order')
        original[name] = list(struct.unpack('<' + str(args.rows) + 'Q', prefix))
    expected = {}
    for name in ('OS', 'URLHash'):
        values = original[name]
        threshold = sorted(values)[len(values) // 2]
        expected['sum_' + name] = dict(sum=str(sum(values)), selected_rows=args.rows, threshold='0', touched_blocks=args.rows // 256)
        selected = [i for i, value in enumerate(values) if value >= threshold]
        expected['count_' + name] = dict(sum='0', selected_rows=len(selected), threshold=str(threshold), touched_blocks=len({i // 256 for i in selected}))
    for name in ('EventTime', 'WatchID'):
        values = original[name]
        threshold = sorted(values)[len(values) * 90 // 100]
        selected = [i for i, value in enumerate(values) if value >= threshold]
        expected['pair_' + name + '_ClientIP'] = dict(sum=str(sum(original['ClientIP'][i] for i in selected)),
            selected_rows=len(selected), threshold=str(threshold), touched_blocks=len({i // 256 for i in selected}))
    cpp_binary = output / 'frame3_duckdb_corpus'
    cpp = ['clang++-21', '-std=c++23', '-O3', '-gline-tables-only', '-Wall', '-Wextra', '-Wpedantic',
           '-march=armv8-a+simd', '-mtune=neoverse-v2', '-fno-fast-math', '-ffp-contract=off',
           str(HERE / 'duckdb.cpp'), '-I' + str(duck), '-L' + str(duck), '-Wl,-rpath,' + str(duck), '-lduckdb', '-o', str(cpp_binary)]
    started = time.monotonic_ns()
    build = subprocess.run(cpp, text=True, capture_output=True)
    cpp_build_ns = time.monotonic_ns() - started
    (output / 'duckdb-build.log').write_text(build.stdout + build.stderr)
    assert build.returncode == 0, build.stderr
    rust = [str(cargo), 'build', '--release', '--locked', '--offline', '--bin', 'frame3-polars-corpus', '--message-format=json-render-diagnostics']
    started = time.monotonic_ns()
    with (output / 'polars-build.jsonl').open('w') as stdout, (output / 'polars-build.log').open('w') as stderr:
        child = subprocess.Popen(rust, cwd=HERE / 'polars', env=env, text=True,
                                 stdout=stdout, stderr=subprocess.PIPE, start_new_session=True)
        for line in child.stderr:
            stderr.write(line)
            if line.strip().startswith('Compiling ') and not line.strip().startswith('Compiling frame-polars-check '):
                os.killpg(child.pid, signal.SIGTERM)
                child.wait()
                raise RuntimeError('Stopped before broad dependency rebuild; cached build mismatch: ' + line.strip())
        status = child.wait()
        stderr.flush()
        assert status == 0, (output / 'polars-build.log').read_text()[-5000:]
    rust_build_ns = time.monotonic_ns() - started
    rust_binary = polars.TASK / 'target/release/frame3-polars-corpus'
    retained_rust = output / rust_binary.name
    shutil.copy2(rust_binary, retained_rust)
    rust_binary = retained_rust
    artifacts = [json.loads(line) for line in (output / 'polars-build.jsonl').read_text().splitlines()]
    artifacts = [record for record in artifacts if record.get('reason') == 'compiler-artifact']
    upstream_after = cached_source_snapshots(polars)
    assert upstream_before == upstream_after
    assert all(sha(ROOT / p) == digest for p, digest in source_sha256.items()), 'Source changed during build'
    assert all(sha(Path(v['path'])) == v['file_sha256'] for v in inputs.values()), 'Input snapshot changed during build'
    receipt = dict(scope='Local Linux AArch64 repeated six-cell observations only; no Zen/Graviton timing, flush, Frame rank, allocator peak or sanitized-engine claim',
        source_sha256=source_sha256, inputs=inputs, expected=expected,
        input_artifact_reference=json.loads(input_reference.read_text()), input_reference_sha256=sha(input_reference),
        upstream=upstream_before, duckdb_library_sha256=sha(duck / 'libduckdb.so'), duckdb_header_sha256=sha(duck / 'duckdb.h'),
        compiler=subprocess.check_output(['clang++-21', '--version'], text=True),
        rustc=subprocess.check_output([str(rustc), '-Vv'], cwd=HERE / 'polars', env=env, text=True),
        host=subprocess.check_output(['uname', '-a'], text=True),
        cpu_inventory=subprocess.check_output(['lscpu'], text=True),
        commands=dict(cpp=cpp, cargo=rust), binaries=dict(duckdb=sha(cpp_binary), polars=sha(rust_binary)),
        build_wall_ns=dict(cpp=cpp_build_ns, cached_rust_leaf_and_link=rust_build_ns),
        fresh_cargo_artifacts=[a['target']['name'] for a in artifacts if not a.get('fresh')],
        cache_policy='Reuse pinned libraries/toolchain/dependencies only; no source download or bootstrap',
        counts=dict(rows=args.rows, rounds=args.rounds, cpu=args.cpu, original_corpus_columns_in_process=len(NAMES)),
        runs=[], preflight=[])
    # Freeze identities and exact expectations BEFORE either timer driver runs.
    (output / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    for label, binary in (('duckdb', cpp_binary), ('polars', rust_binary)):
        command = ['taskset', '-c', str(args.cpu), str(binary), '--check-only']
        run = subprocess.run(command, env=env, text=True, capture_output=True)
        (output / (label + '-preflight.json')).write_text(run.stdout)
        (output / (label + '-preflight.stderr')).write_text(run.stderr)
        assert run.returncode == 0, (label, run.stderr)
        result = json.loads(run.stdout)
        assert result == {'checks': 108, 'pass': True}
        receipt['preflight'].append(dict(engine=label, command=command, result=result))
    assert all(sha(ROOT / p) == digest for p, digest in source_sha256.items()), 'Source changed during preflight'
    assert all(sha(Path(v['path'])) == v['file_sha256'] for v in inputs.values()), 'Input snapshot changed during preflight'
    assert receipt['binaries'] == dict(duckdb=sha(cpp_binary), polars=sha(rust_binary)), 'Binary changed during preflight'
    if args.prepare_only:
        (output / 'prepared.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(dict(prepared=str(output / 'prepared.json'), timed=False, preflight=receipt['preflight'])))
        return
    admission = admit_timing(args.cpu)
    receipt['timing_admission'] = admission
    if not admission['accepted']:
        (output / 'timing-rejected.json').write_text(json.dumps(receipt, indent=2) + '\n')
        raise RuntimeError('Query timers not started: active compiler or selected CPU less than 90% idle; see timing-rejected.json')
    # Refresh the frozen pre-timer receipt with the accepted host-load evidence.
    (output / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    for label, binary in (('duckdb', cpp_binary), ('polars', rust_binary)):
        command = ['taskset', '-c', str(args.cpu), str(binary), str(snapshots), str(args.rows), str(args.rounds)]
        run = subprocess.run(command, env=env, text=True, capture_output=True)
        (output / (label + '.jsonl')).write_text(run.stdout)
        (output / (label + '.stderr')).write_text(run.stderr)
        assert run.returncode == 0, (label, run.stderr)
        records = [json.loads(line) for line in run.stdout.splitlines()]
        assert len(records) == 2 * len(expected) and {(r['case'], r['arm']) for r in records} == {
            (case, arm) for case in expected for arm in ('sidecar_uint64', 'external_typed_for')}
        for record in records:
            assert all(record[key] == value for key, value in expected[record['case']].items()), record
            assert len(record['samples_ns']) == args.rounds and all(x > 0 for x in record['samples_ns'])
            assert abs(record['median_ns'] - statistics.median(record['samples_ns'])) <= 1
        receipt['runs'].append(dict(engine=label, command=command, exit_code=run.returncode,
            records=records, stdout_sha256=sha(output / (label + '.jsonl')), stderr_sha256=sha(output / (label + '.stderr'))))
    receipt['load_after_queries'] = load_snapshot(args.cpu)
    if receipt['load_after_queries']['active_compilers']:
        (output / 'timing-rejected.json').write_text(json.dumps(receipt, indent=2) + '\n')
        raise RuntimeError('Compiler overlap detected after queries; results rejected, see timing-rejected.json')
    assert all(sha(Path(v['path'])) == v['file_sha256'] for v in inputs.values()), 'Input changed during run'
    assert all(sha(ROOT / p) == digest for p, digest in receipt['source_sha256'].items()), 'Source changed during run'
    assert receipt['binaries'] == dict(duckdb=sha(cpp_binary), polars=sha(rust_binary))
    (output / 'qualification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(receipt=str(output / 'qualification.json'), engines=len(receipt['runs']), cells_per_engine=6,
        arms_per_cell=2, fresh_cargo_artifacts=receipt['fresh_cargo_artifacts'], pass_=True)))


if __name__ == '__main__':
    main()
