#!/usr/bin/env python3
"""Qualify native pinned Polars queries; one local build job, no timing panel."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tomllib
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[5]
TASK = ROOT / 'build/frame-helper/polars'
REVISION = '0df0c25d4db895ec8cad773bedc4e98400e3135e'
OBJECT_STORE = 'f50a6e5c564b2b5933eca15cd20ff9b5614374a1'
SOURCES = [
    ('polars', REVISION, 'pola-rs/polars', '2bab6c94274373e810645df3160077a1fe3bd9b64cce2b4f0eff88c06b0b566d'),
    ('object-store', OBJECT_STORE, 'kdn36/arrow-rs-object-store', '70c09d5e6e24c5d25086b27bb9e212b89151e385a3054266d436c54cc75458d9'),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(url, path, expected):
    if not path.exists():
        temporary = path.with_suffix('.download')
        with urllib.request.urlopen(url, timeout=60) as src, temporary.open('wb') as dst:
            while chunk := src.read(1024 * 1024):
                dst.write(chunk)
        assert sha(temporary) == expected, url
        temporary.rename(path)
    assert sha(path) == expected, path


def source_snapshots():
    result = {}
    for label, revision, repo, expected in SOURCES:
        archive = TASK / f'{label}-{revision}.tar.gz'
        url = f'https://codeload.github.com/{repo}/tar.gz/{revision}'
        download(url, archive, expected)
        with tarfile.open(archive) as stream:
            prefix = stream.getmembers()[0].name.split('/')[0]
            destination = TASK / 'sources' / prefix
            if not destination.exists():
                stream.extractall(TASK / 'sources', filter='data')
            hashes = {}
            for member in stream.getmembers():
                if member.isfile():
                    name = str(Path(*Path(member.name).parts[1:]))
                    hashes[name] = hashlib.sha256(stream.extractfile(member).read()).hexdigest()
                    assert sha(destination / name) == hashes[name], name
        snapshot = hashlib.sha256(json.dumps(hashes, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        result[label] = dict(revision=revision, url=url, archive_sha256=expected,
            source_files=len(hashes), snapshot_sha256=snapshot)
    return result


def environment():
    env = os.environ.copy()
    # Task-local installation/cache only; never source an env file or change
    # the user's Rust defaults, profiles, Cargo config or global cache.
    env.update(RUSTUP_HOME=str(TASK / 'rustup'), CARGO_HOME=str(TASK / 'cargo'),
        CARGO_TARGET_DIR=str(TASK / 'target'), CARGO_BUILD_JOBS='1',
        RUSTFLAGS='-C target-cpu=generic -C target-feature=+neon',
        POLARS_MAX_THREADS='1', CC='clang-21', CXX='clang++-21')
    env['PATH'] = str(TASK / 'cargo/bin') + ':' + env['PATH']
    for name in ['CARGO_ENCODED_RUSTFLAGS', 'RUSTC', 'RUSTC_WRAPPER', 'RUSTC_WORKSPACE_WRAPPER']:
        env.pop(name, None)
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bootstrap', action='store_true', help='Install task-local pinned Rust if absent')
    args = parser.parse_args()
    assert platform.system() == 'Linux' and platform.machine() == 'aarch64'
    TASK.mkdir(parents=True, exist_ok=True)
    output = TASK / 'evidence'
    output.mkdir(exist_ok=True)
    env = environment()
    cargo = TASK / 'cargo/bin/cargo'
    if args.bootstrap and not (TASK / 'rustup/toolchains/nightly-2026-04-01-aarch64-unknown-linux-gnu/bin/rustc').exists():
        installer = TASK / 'rustup-init'
        download('https://static.rust-lang.org/rustup/archive/1.29.2/aarch64-unknown-linux-gnu/rustup-init',
            installer, 'a9c168dbea81a84671c6b7a5c1795cb0eb699020110d107ddfca427bd797e697')
        installer.chmod(0o755)
        subprocess.run([str(installer), '-y', '--no-modify-path', '--profile', 'minimal',
            '--default-toolchain', 'nightly-2026-04-01'], env=env, check=True)
    rustc = subprocess.check_output([str(TASK / 'cargo/bin/rustc'), '-Vv'], cwd=HERE, env=env, text=True)
    assert '48cc71ee88cd0f11217eced958b9930970da998b' in rustc and 'aarch64-unknown-linux-gnu' in rustc
    before = source_snapshots()
    owned = {str(p.relative_to(HERE)): sha(p) for p in [HERE / 'Cargo.toml', HERE / 'Cargo.lock',
        HERE / 'rust-toolchain.toml', HERE / 'src/main.rs', HERE / 'run.py']}
    metadata_command = [str(cargo), 'metadata', '--locked', '--filter-platform', 'aarch64-unknown-linux-gnu', '--format-version=1']
    metadata = subprocess.check_output(metadata_command, cwd=HERE, env=env, text=True)
    (output / 'metadata.json').write_text(metadata)
    command = ['/usr/bin/time', '-v', 'taskset', '-c', '8', str(cargo), 'build', '--release', '--locked',
        '--message-format=json-render-diagnostics']
    with (output / 'build.jsonl').open('w') as stdout, (output / 'build.log').open('w') as stderr:
        build = subprocess.run(command, cwd=HERE, env=env, stdout=stdout, stderr=stderr)
    assert build.returncode == 0, (output / 'build.log').read_text()[-6000:]
    binary = TASK / 'target/release/frame-polars-check'
    run_command = ['taskset', '-c', '8', str(binary)]
    run = subprocess.run(run_command, env=env, cwd=HERE, text=True, capture_output=True)
    (output / 'checks.jsonl').write_text(run.stdout)
    (output / 'plans.txt').write_text(run.stderr)
    assert run.returncode == 0, run.stderr[-6000:]
    records = [json.loads(line) for line in run.stdout.splitlines()]
    summary = records[-1]
    assert summary['pass'] and summary['queries'] == 12960 and summary['scalar_checks'] == 38880
    assert not summary['timing_claim']
    assert source_snapshots() == before
    assert owned == {name: sha(HERE / name) for name in owned}
    artifacts = [json.loads(line) for line in (output / 'build.jsonl').read_text().splitlines()]
    artifacts = [entry for entry in artifacts if entry['reason'] == 'compiler-artifact']
    packages = {entry['id']: entry for entry in json.loads(metadata)['packages']}
    locked = {(entry['name'], entry['version']): entry['checksum'] for entry in
        tomllib.loads((HERE / 'Cargo.lock').read_text())['package'] if 'checksum' in entry}
    registry_archives = {}
    for package_id in sorted({entry['package_id'] for entry in artifacts}):
        package = packages[package_id]
        if package['source'] and package['source'].startswith('registry+'):
            name, version = package['name'], package['version']
            archives = list((TASK / 'cargo/registry/cache').glob(f'*/{name}-{version}.crate'))
            assert len(archives) == 1 and sha(archives[0]) == locked[name, version]
            registry_archives[f'{name}@{version}'] = locked[name, version]
    native = [entry for entry in artifacts if entry['target']['name'] in [
        'polars', 'polars_core', 'polars_arrow', 'polars_compute', 'polars_lazy',
        'polars_plan', 'polars_expr', 'polars_mem_engine', 'polars_ops', 'polars_io', 'object_store']]
    native_by_name = {entry['target']['name']: entry for entry in native}
    assert all(entry['profile']['opt_level'] == '3' for entry in native)
    assert {'nightly', 'simd'} <= set(native_by_name['polars_core']['features'])
    assert 'cse' in native_by_name['polars_plan']['features']
    assert 'cloud' not in native_by_name['polars_io']['features'] and 'object_store' not in native_by_name
    # Cargo metadata is a wider resolution feature union; these are the
    # compiler's actual release features and target flags, including fresh
    # incremental artifacts. Do not substitute metadata's union for this.
    fingerprints = list((TASK / 'target/release/.fingerprint').glob('polars-core-*/lib-polars_core.json'))
    assert len(fingerprints) == 1
    core_fingerprint = json.loads(fingerprints[0].read_text())
    assert core_fingerprint['rustflags'] == ['-C', 'target-cpu=generic', '-C', 'target-feature=+neon']
    symbols = subprocess.check_output(['llvm-nm-21', '--defined-only', '--demangle', str(binary)], text=True)
    selected_symbols = [line for line in symbols.splitlines() if any(name in line for name in
        ['polars_mem_engine::executors::filter', 'polars_mem_engine::executors::projection',
         'polars_compute::filter', 'polars_core::chunked_array::ops::aggregate'])]
    (output / 'linked-symbols.txt').write_text('\n'.join(selected_symbols) + '\n')
    receipt = dict(owned_inputs_sha256=owned, inputs_unchanged=True, upstream=before,
        rustc_Vv=rustc, cargo_Vv=subprocess.check_output([str(cargo), '-Vv'], env=env, cwd=HERE, text=True),
        host=subprocess.check_output(['uname', '-a'], text=True),
        environment={key: env[key] for key in ['RUSTUP_HOME', 'CARGO_HOME', 'CARGO_TARGET_DIR',
            'CARGO_BUILD_JOBS', 'RUSTFLAGS', 'POLARS_MAX_THREADS', 'CC', 'CXX']},
        commands=[metadata_command, command, run_command], native_artifacts=native,
        compiled_registry_archives_sha256=registry_archives,
        actual_core_fingerprint=core_fingerprint,
        metadata_sha256=sha(output / 'metadata.json'), build_log_sha256=sha(output / 'build.log'),
        binary_sha256=sha(binary), result=summary, accumulators=records[-2],
        checks_sha256=sha(output / 'checks.jsonl'), plans_sha256=sha(output / 'plans.txt'),
        linked_symbols=selected_symbols, dynamic_libraries=subprocess.check_output(['ldd', str(binary)], text=True),
        scope='Optimized native Linux AArch64 functional control. Actual LazyFrame filtering/reductions and returned scalars. System allocator, no Python engine, sanitizer, native x86, speed, imported-buffer or wheel-equivalence claim.')
    (output / 'runs.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(summary))
    print(json.dumps(dict(receipt=str(output / 'runs.json'), pass_checks=True)))


if __name__ == '__main__':
    main()
