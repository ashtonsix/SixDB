#!/usr/bin/env python3
"""Independent ordinary-workflow counterexamples; only private fixtures are written.

Run on Linux from the repository root. No AWS, network, real cache, benchmark,
or production source writes. A local verified archive copy substitutes only the
object transport; collection/cache/extraction remain the real helper code.
"""
import argparse
from contextlib import contextmanager, redirect_stdout
import fcntl
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import random
import shutil
import signal
import struct
import subprocess
import sys
import tarfile
import time


ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / 'workbench/tools'
STORE = ROOT / 'workbench/spikes/storage-lifecycle/store.py'
sys.path.insert(0, str(TOOLS))
import artifacts
import capture
import datasets
import experiment
import local_cache
import storage
import worker_cache


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def allocation(path):
    """Independent directory and regular-file st_blocks sum, deduped by inode."""
    found = set()
    total = 0
    for p in [Path(path), *Path(path).rglob('*')]:
        info = p.lstat()
        identity = info.st_dev, info.st_ino
        if identity not in found:
            total += info.st_blocks * 512
            found.add(identity)
    return total


def extents(path):
    """Best-effort independent FIEMAP report; not underlying device accounting."""
    count = 32
    request = bytearray(struct.pack('=QQIIII', 0, 2**64 - 1, 1, 0, count, 0) + bytes(56 * count))
    try:
        with Path(path).open('rb') as handle:
            fcntl.ioctl(handle.fileno(), 0xC020660B, request, True)
        mapped = struct.unpack_from('=I', request, 20)[0]
        return [dict(zip(['logical', 'physical', 'length', 'reserved0', 'reserved1',
                          'flags', 'reserved2', 'reserved3', 'reserved4'],
                         struct.unpack_from('=QQQQQIIII', request, 32 + 56 * i)))
                for i in range(mapped)]
    except OSError as error:
        return {'unavailable': str(error)}


@contextmanager
def env(**values):
    before = {key: os.environ.get(key) for key in values}
    os.environ.update({key: str(value) for key, value in values.items()})
    try:
        yield
    finally:
        for key, value in before.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def git(root, *args):
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def repository(root, payload):
    root.mkdir(parents=True)
    git(root, 'init', '-q')
    (root / '.gitignore').write_text('build/\n')
    (root / 'tables.inc').write_bytes(payload)
    git(root, 'add', '.')
    git(root, '-c', 'user.name=Storage fixture', '-c', 'user.email=fixture@invalid',
        'commit', '-qm', 'Private fixture')
    return root


def bundle(root, members):
    archive = root / 'object.tar.gz'
    with tarfile.open(archive, 'w:gz') as out:
        for name, data in members.items():
            member = tarfile.TarInfo(name)
            member.size = len(data)
            out.addfile(member, io.BytesIO(data))
    return archive, {'kind': 'files', 'bucket': 'fixture', 'key': 'local-only',
                     'region': 'fixture', 'bytes': archive.stat().st_size,
                     'sha256': digest(archive), 'files': len(members)}


@contextmanager
def local_object(archive):
    previous = artifacts.download
    def download(reference, destination):
        shutil.copyfile(archive, destination)
        assert destination.stat().st_size == reference['bytes']
        assert digest(destination) == reference['sha256']
    artifacts.download = download
    try:
        yield
    finally:
        artifacts.download = previous


def nested_collection(root, payload):
    repo = repository(root / 'repo', payload)
    raw = root / 'producer'
    run = experiment.Run(repo, raw / 'comparison', {}, workspace=repo / 'build/workspace')
    assert run.finish() is None
    nested = raw / 'comparison/source.tar.gz'
    source_hash = digest(nested)
    archive, reference = bundle(root, {'source.tar.gz': nested.read_bytes(),
                                      'comparison/source.tar.gz': nested.read_bytes(),
                                      'comparison/run.json': (raw / 'comparison/run.json').read_bytes(),
                                      'worker-result.json': b'{"state":"complete"}\n'})
    allocations = []
    budget = 256 * 1024
    with local_object(archive), env(SIXDB_WORKER_CACHE_GIB=budget / storage.GIB):
        for i in range(6):
            job = repo / 'build/workers' / f'job-{i}'
            job.mkdir(parents=True)
            storage.write_json(job / 'artifact.json', reference)
            storage.write_json(job / 'status.json', {'state': 'complete'})
            worker_cache.preflight(repo)
            worker_cache.collect(job, reference)
            assert not (job / 'results/source.tar.gz').exists()
            assert digest(job / 'results/comparison/source.tar.gz') == source_hash
            assert worker_cache.verified(job, reference)
            allocations.append(allocation(repo / 'build/workers'))
        entries = worker_cache.inventory(repo)
    assert allocations[-1] > budget
    return {'budget_bytes': budget, 'jobs': len(allocations),
            'allocated_progression': allocations,
            'nested_source_bytes': nested.stat().st_size, 'nested_source_sha256': source_hash,
            'nested_source_extents': [extents(repo / f'build/workers/job-{i}/results/comparison/source.tar.gz')
                                      for i in range(2)],
            'inventory_bytes': sum(entry['bytes'] for entry in entries),
            'prunable_now': sum(entry['prunable_bytes'] for entry in entries if entry['eligible'])}


def overlapping_inputs(root, payload):
    members = {'columns/shared.bin': payload}
    members.update({f'views/view-{i}.json': json.dumps({'view': i}).encode() for i in range(6)})
    archive, reference = bundle(root, members)
    path = root / 'artifact.json'
    storage.write_json(path, reference)
    cache = root / 'cache'
    with local_object(archive):
        directories = [artifacts.cached_input(path, selected=['columns/shared.bin', f'views/view-{i}.json'],
                                              cache_root=cache) for i in range(6)]
        reused = artifacts.cached_input(path, selected=['views/view-0.json', 'columns/shared.bin'], cache_root=cache)
    files = [directory / 'columns/shared.bin' for directory in directories]
    assert reused == directories[0]
    assert all(digest(p) == hashlib.sha256(payload).hexdigest() for p in files)
    assert len({p.stat().st_ino for p in files}) == len(files)
    return {'views': len(files), 'shared_content_bytes': len(payload),
            'shared_content_sha256': digest(files[0]),
            'common_member_allocated_bytes': sum(p.stat().st_blocks * 512 for p in files),
            'cache_allocated_bytes': allocation(cache),
            'first_two_extent_maps': [extents(p) for p in files[:2]],
            'same_selection_reused': reused == directories[0]}


def dataset_variants(root, payload):
    recipe = root / 'prepare.py'
    recipe.write_text('# Private deterministic preparation fixture\n')
    outputs = []
    calls = 0
    def build(destination):
        nonlocal calls
        calls += 1
        (destination / 'shared.bin').write_bytes(payload)
        return {'same_projection': True}
    with env(SIXDB_DATA_CACHE=root / 'cache'):
        for i in range(8):
            outputs.append(datasets.cached('projection', recipe, {'input': hashlib.sha256(payload).hexdigest()},
                                           {'sample_seed': i}, build))
        again = datasets.cached('projection', recipe, {'input': hashlib.sha256(payload).hexdigest()},
                                {'sample_seed': 0}, build)
        assert again == outputs[0]
        assert calls == 8
        identities = [datasets.verify(path)['id'] for path in outputs]
    assert len(set(identities)) == 8
    assert {digest(path / 'shared.bin') for path in outputs} == {hashlib.sha256(payload).hexdigest()}
    return {'variants': len(outputs), 'build_calls': calls,
            'identical_payload_allocated_bytes': sum((path / 'shared.bin').stat().st_blocks * 512 for path in outputs),
            'allocated_cache_bytes': allocation(root / 'cache'), 'same_request_reused': again == outputs[0]}


def interrupted_recovery(root, payload):
    archive, reference = bundle(root, {'columns/shared.bin': payload})
    reference_path = root / 'artifact.json'
    storage.write_json(reference_path, reference)
    cache = root / 'cache'
    child = '''import hashlib,os,pathlib,shutil,signal,sys
sys.path.insert(0,sys.argv[1])
import artifacts
archive,reference,cache,ready=map(pathlib.Path,sys.argv[2:])
def download(expected,destination):
    shutil.copyfile(archive,destination)
    assert destination.stat().st_size==expected['bytes']
    assert hashlib.sha256(destination.read_bytes()).hexdigest()==expected['sha256']
    ready.write_text('verified local transport complete')
    os.kill(os.getpid(),signal.SIGSTOP)
artifacts.download=download
artifacts.cached_input(reference,cache_root=cache)
'''
    progress = []
    for i in range(4):
        ready = root / f'ready-{i}'
        process = subprocess.Popen([sys.executable, '-c', child, str(TOOLS), str(archive),
                                    str(reference_path), str(cache), str(ready)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 10
        while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.01)
        assert ready.exists(), 'private interrupted-transfer child did not reach the intended boundary'
        process.kill()
        _, errors = process.communicate(timeout=5)
        assert process.returncode == -signal.SIGKILL, errors
        progress.append(allocation(cache))
    leftovers = list(cache.rglob('bundle.tar.gz'))
    assert len(leftovers) == 4
    assert all(digest(path) == digest(archive) for path in leftovers)
    with local_object(archive):
        result = artifacts.cached_input(reference_path, cache_root=cache)
    assert digest(result / 'columns/shared.bin') == hashlib.sha256(payload).hexdigest()
    assert all(path.exists() for path in leftovers)
    return {'interrupted_recoveries': len(leftovers), 'allocated_after_each_kill': progress,
            'verified_leftover_archive_bytes': sum(path.stat().st_size for path in leftovers),
            'after_successful_retry_allocated_bytes': allocation(cache),
            'old_temporary_archives_still_present_after_success': len(leftovers)}


def worktree_builds(root, payload):
    repo = repository(root / 'repo', payload)
    other = root / 'captured'
    capture.capture(repo, other)
    objects = []
    for checkout in [repo, other]:
        for preset in ['dev', 'release']:
            build = checkout / f'build/clang/{preset}'
            build.mkdir(parents=True)
            (build / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release\n')
            obj = build / 'CMakeFiles/probe.dir/probe.cpp.o'
            obj.parent.mkdir(parents=True)
            obj.write_bytes(b'\x7fELF' + payload[4:])
            objects.append(obj)
    budget = 256 * 1024
    with env(SIXDB_LOCAL_BUILD_GIB=budget / storage.GIB, XDG_CACHE_HOME=root / 'xdg'):
        with redirect_stdout(io.StringIO()):
            report = local_cache.maintain(repo, prune=True)
        assert report['over_budget'] and report['removed_bytes'] == 0
        assert all(p.exists() for p in objects)
        # New build writes are not admitted through the reporting policy.
        extra = other / 'build/clang/dev/CMakeFiles/probe.dir/second.cpp.o'
        extra.write_bytes(b'\x7fELF' + payload[4:])
        (other / 'build/.keep-local').touch()
        with env(SIXDB_LOCAL_BUILD_DAYS=0), redirect_stdout(io.StringIO()):
            pinned_report = local_cache.maintain(repo, prune=True)
        assert extra.exists()
    return {'checkouts_found': sorted(str(p) for p in worker_cache.checkouts(repo)),
            'budget_bytes': budget, 'reported_bytes': report['bytes'],
            'recent_removed_bytes': report['removed_bytes'], 'after_new_write_bytes': pinned_report['bytes'],
            'pinned_new_write_survived': extra.exists(),
            'source_hashes_equal': digest(repo / 'tables.inc') == digest(other / 'tables.inc')}


def store_cli(root, budget, *args, check=False):
    result = subprocess.run([sys.executable, str(STORE), '--root', str(root), '--budget', str(budget), *map(str, args)],
                            capture_output=True, text=True)
    if check:
        assert result.returncode == 0, result.stderr
    return {'returncode': result.returncode, 'stdout': result.stdout.strip(), 'stderr': result.stderr.strip()}


def readers(root, payload):
    budget = 256 * 1024
    source = root / 'backend'
    source.mkdir()
    a, b = source / 'a', source / 'b'
    # Objects individually fit, and together exceed the store budget.
    a.write_bytes(payload[:160 * 1024])
    b.write_bytes(bytes(value ^ 0x55 for value in payload[:160 * 1024]))
    store = root / 'open-reader'
    first = store_cli(store, budget, 'fetch', 'a', a, '--sha256', digest(a), check=True)
    returned = json.loads(store_cli(store, budget, 'get', 'a', check=True)['stdout'])
    path = Path(returned['path'])
    with path.open('rb') as reader:
        second = store_cli(store, budget, 'fetch', 'b', b, '--sha256', digest(b), check=True)
        visible = json.loads(store_cli(store, budget, 'stats', check=True)['stdout'])
        held = os.fstat(reader.fileno()).st_blocks * 512
        content = reader.read()
        assert content == a.read_bytes()
        assert not path.exists()
        assert visible['allocated_bytes'] <= budget < visible['allocated_bytes'] + held
        open_result = {'visible_allocated_bytes': visible['allocated_bytes'], 'reader_unlinked_allocated_bytes': held,
                       'combined_live_allocated_bytes': visible['allocated_bytes'] + held,
                       'returned_path_exists': path.exists(), 'reader_content_sha256': hashlib.sha256(content).hexdigest()}
    leased = root / 'crashed-reader'
    consumer = subprocess.run([sys.executable, '-c',
        'import os,signal,subprocess,sys; '
        'r=subprocess.run(sys.argv[1:]); '
        'sys.exit(r.returncode) if r.returncode else os.kill(os.getpid(),signal.SIGKILL)',
        sys.executable, str(STORE), '--root', str(leased), '--budget', str(budget),
        'fetch', 'a', str(a), '--sha256', digest(a), '--lease', 'research-reader'],
        capture_output=True, text=True)
    assert consumer.returncode == -signal.SIGKILL
    refused = store_cli(leased, budget, 'fetch', 'b', b, '--sha256', digest(b))
    assert refused['returncode'] == 2 and 'leases=' in refused['stderr']
    stale = json.loads(store_cli(leased, budget, 'stats', check=True)['stdout'])
    store_cli(leased, budget, 'release', 'research-reader', check=True)
    recovered = store_cli(leased, budget, 'fetch', 'b', b, '--sha256', digest(b), check=True)
    return {'budget_bytes': budget, 'unleased_reader': open_result, 'consumer_returncode': consumer.returncode,
            'stale_lease_stats': stale, 'next_job_refusal': refused, 'after_manual_release': recovered['returncode']}


def metadata_history(root):
    budget = 256 * 1024
    backend = root / 'backend'
    backend.mkdir()
    store = root / 'store'
    admitted = 0
    refusal = None
    for i in range(600):
        source = backend / f'{i:04}'
        source.write_text(f'normal-job-{i}\n')
        result = store_cli(store, budget, 'fetch', 'latest', source, '--sha256', digest(source))
        if result['returncode']:
            refusal = result
            break
        admitted += 1
    assert refusal is not None
    catalog = json.loads((store / 'catalog.json').read_text())
    entries = list((store / 'objects').iterdir())
    latest = store_cli(store, budget, 'get', 'latest')
    return {'budget_bytes': budget, 'admitted_before_refusal': admitted,
            'catalog_bytes': (store / 'catalog.json').stat().st_size,
            'historical_object_entries': len(catalog['objects']), 'keys': len(catalog['keys']),
            'resident_objects': len(entries), 'resident_payload_bytes': sum(p.stat().st_size for p in entries),
            'independent_allocated_bytes': allocation(store), 'next_job_refusal': refusal,
            'latest_get_after_refusal': latest}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/reviews/storage-20261024/fresh-workflow')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Choose a new isolated output; existing fixtures are preserved')
    output.mkdir(parents=True)
    paths = [STORE, Path(__file__), *[TOOLS / name for name in
             ['artifacts.py', 'capture.py', 'datasets.py', 'experiment.py', 'local_cache.py',
              'storage.py', 'worker.py', 'worker_cache.py', 'worker_runtime.py']]]
    hashes = {str(path.relative_to(ROOT)): digest(path) for path in paths}
    payload = random.Random(20261024).randbytes(192 * 1024)
    results = {'source_sha256_before': hashes, 'payload_sha256': hashlib.sha256(payload).hexdigest(),
               'measurement': 'Linux st_blocks*512; FIEMAP is filesystem extent evidence, not backing-device enforcement'}
    with env(PYTHONDONTWRITEBYTECODE=1, SIXDB_MIN_FREE_GIB=0, XDG_CACHE_HOME=output / 'xdg'):
        for name, operation in [('nested_collection', nested_collection), ('overlapping_inputs', overlapping_inputs),
                                ('dataset_variants', dataset_variants), ('interrupted_recovery', interrupted_recovery),
                                ('worktree_builds', worktree_builds),
                                ('readers', readers), ('metadata_history', metadata_history)]:
            home = output / name
            home.mkdir()
            results[name] = operation(home, payload) if name != 'metadata_history' else operation(home)
            (output / 'results.json').write_text(json.dumps(results, indent=2, sort_keys=True) + '\n')
            print(json.dumps({'finished': name, 'result': results[name]}, sort_keys=True), flush=True)
    results['source_sha256_after'] = {str(path.relative_to(ROOT)): digest(path) for path in paths}
    results['sources_unchanged'] = results['source_sha256_after'] == hashes
    (output / 'results.json').write_text(json.dumps(results, indent=2, sort_keys=True) + '\n')
    assert results['sources_unchanged'], 'A reviewed source changed during the review; retain both epochs'


if __name__ == '__main__':
    main()
