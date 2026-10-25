#!/usr/bin/env python3
"""External repeated-job storage workload; fixtures and the object service are local.

The oracle walks the filesystem itself. It never uses a tool-reported usage
counter to decide whether the configured retained-byte bound was respected.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tarfile
import threading
import time


REPO = Path(__file__).resolve().parents[4]
MIB = 1024 ** 2
SHARED = bytes(range(256)) * 256  # 64 KiB, shared by every job and variant.
SOURCE = b'captured source bytes\n' * 512


def digest(data):
    return hashlib.sha256(data).hexdigest()


def disk_walk(root):
    """Independent lstat accounting; count each regular inode only once."""
    seen = set()
    logical = regular = directories = links = 0
    allocated = root.lstat().st_blocks * 512 if root.exists() else 0
    for base, children, names in os.walk(root, followlinks=False):
        for name in [*children, *names]:
            p = Path(base) / name
            try:
                s = p.lstat()
            except FileNotFoundError:
                continue
            if stat.S_ISLNK(s.st_mode):
                links += 1
                continue
            if stat.S_ISDIR(s.st_mode):
                directories += 1
                allocated += s.st_blocks * 512
            elif stat.S_ISREG(s.st_mode) and (s.st_dev, s.st_ino) not in seen:
                seen.add((s.st_dev, s.st_ino))
                regular += 1
                logical += s.st_size
                allocated += s.st_blocks * 512
    return dict(logical_bytes=logical, allocated_bytes=allocated,
                regular_inodes=regular, directories=directories, symlinks=links)


class Peak:
    def __init__(self, root):
        self.root, self.stop, self.maximum = root, threading.Event(), 0
        self.thread = threading.Thread(target=self.sample, daemon=True)

    def sample(self):
        while not self.stop.is_set():
            self.maximum = max(self.maximum, disk_walk(self.root)['allocated_bytes'])
            self.stop.wait(.025)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join()
        self.maximum = max(self.maximum, disk_walk(self.root)['allocated_bytes'])


def object_bundle(service, members):
    """Build independently of artifacts.pack, using an ordinary Python tar writer."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode='w:gz') as archive:
        for name, data in members.items():
            member = tarfile.TarInfo(name)
            member.mode, member.size, member.mtime = 0o644, len(data), 0
            archive.addfile(member, io.BytesIO(data))
    data = raw.getvalue()
    checksum = digest(data)
    key = 'bundles/' + checksum + '.tar.gz'
    target = service / 'fixture' / key
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return dict(format=1, kind='files', bucket='fixture', region='test', key=key,
                bytes=len(data), sha256=checksum, files=len(members))


def setup(root, repo):
    root.mkdir(parents=True, exist_ok=False)
    controller = root / 'controller'
    tools = controller / 'workbench/tools'
    tools.mkdir(parents=True)
    # A private controller makes ROOT/build/workers the actual budget scope.
    source_hashes = {}
    for p in sorted((repo / 'workbench/tools').glob('*.py')):
        data = p.read_bytes()
        (tools / p.name).write_bytes(data)
        source_hashes[p.name] = digest(data)
    shutil.copyfile(repo / 'workbench/tools/worker.json', tools / 'worker.json')
    service, bin_dir = root / 'object-service', root / 'bin'
    service.mkdir()
    bin_dir.mkdir()
    stub = bin_dir / 'aws'
    stub.write_text('''#!/usr/bin/env python3
import json,os,pathlib,shutil,sys
a=sys.argv[1:]
root=pathlib.Path(os.environ['STORAGE_TEST_OBJECTS'])
with (root/'requests.jsonl').open('a') as f:f.write(json.dumps(a)+'\\n')
if a[:2]!=['s3api','get-object']:
 sys.exit('fixture refuses every command except s3api get-object')
bucket=a[a.index('--bucket')+1];key=a[a.index('--key')+1]
p=root/bucket/key
if not p.is_file():sys.exit('fixture object missing: '+key)
dest=pathlib.Path(a[a.index('--key')+2])
shutil.copyfile(p,dest)
print(json.dumps({'ContentLength':p.stat().st_size}))
''')
    stub.chmod(0o755)
    env = os.environ.copy()
    env.update(PATH=str(bin_dir) + os.pathsep + env['PATH'],
               STORAGE_TEST_OBJECTS=str(service), SIXDB_MIN_FREE_GIB='0',
               XDG_CACHE_HOME=str(root / 'runtime-cache'),
               SIXDB_WORKER_CACHE_GIB=str(1 / 1024),
               SIXDB_LOCAL_BUILD_GIB=str(1 / 1024),
               SIXDB_DATA_CACHE=str(controller / 'build/datasets'))
    (root / 'tool-source-sha256.json').write_text(json.dumps(source_hashes, indent=2) + '\n')
    return tools, service, env


def call(argv, env, log):
    result = subprocess.run(argv, env=env, text=True, capture_output=True, timeout=30)
    with log.open('a') as f:
        f.write(json.dumps(dict(argv=list(map(str, argv)), code=result.returncode,
                               stdout=result.stdout, stderr=result.stderr)) + '\n')
    return result


def legacy_workers(root, repo, count):
    tools, service, env = setup(root, repo)
    jobs = root / 'controller/build/workers'
    jobs.mkdir(parents=True)
    requests = root / 'process-results.jsonl'
    accepted = failures = 0
    samples = []
    edited = b'{"local": "edited analysis to preserve"}\n'
    with Peak(jobs) as peak:
        for i in range(count):
            name = f'20200101T000000Z-test{i:04}'
            job = jobs / name
            job.mkdir()
            reference = object_bundle(service, {
                'in/inputs/column.u64': SHARED,
                'source.tar.gz': SOURCE,
                'measurement.json': json.dumps({'job': i, 'answer': 42}).encode(),
            })
            prefix = 'jobs/' + name
            remote = service / 'fixture' / prefix
            remote.mkdir(parents=True)
            (remote / 'status.json').write_text(json.dumps({'state': 'complete', 'artifact': reference}))
            (job / 'job.json').write_text(json.dumps({
                'id': name, 'prefix': prefix, 'config': {'bucket': 'fixture', 'region': 'test'}}))
            (job / 'source.tar.gz').write_bytes(SOURCE)
            if i == 0:
                (job / '.keep-local').touch()
            result = call([sys.executable, str(tools / 'worker.py'), 'fetch', str(job)], env, requests)
            accepted += result.returncode == 0
            failures += result.returncode != 0
            if result.returncode == 0:
                # Independent useful-outcome oracle, not checks of emitted manifests.
                assert json.loads((job / 'results/measurement.json').read_text()) == {'job': i, 'answer': 42}
                assert (job / 'results/in/inputs/column.u64').read_bytes() == SHARED
                if i == 1:
                    (job / 'results/measurement.json').write_bytes(edited)
                    repaired = call([sys.executable, str(tools / 'worker.py'), 'fetch', str(job)], env, requests)
                    assert repaired.returncode == 0, repaired.stderr
                    assert (job / 'results/measurement.json').read_bytes() != edited
                    assert any(p.read_bytes() == edited for p in job.glob('replaced-results-*/measurement.json'))
                if i in {2, 3}:
                    selection = ['--full'] if i == 2 else ['--file', 'source.tar.gz']
                    recovered = call([sys.executable, str(tools / 'worker.py'), 'fetch', str(job), *selection], env, requests)
                    assert recovered.returncode == 0, recovered.stderr
                    assert (job / 'results/source.tar.gz').read_bytes() == SOURCE
            if i + 1 in {1, 10, 50, 100, count}:
                samples.append({'jobs': i + 1, **disk_walk(jobs)})
    assert (jobs / '20200101T000000Z-test0000/source.tar.gz').read_bytes() == SOURCE
    final = disk_walk(jobs)
    return dict(case='legacy-ordinary-workers', jobs=count, accepted=accepted, refused=failures,
                configured_budget_bytes=MIB, repeated_input_bytes=len(SHARED),
                independently_unique_input_bytes=len(SHARED), final=final,
                observed_peak_allocated_bytes=peak.maximum, samples=samples,
                pinned_source_and_changed_analysis_preserved=True,
                bounded=peak.maximum <= MIB,
                leftover_staging=[str(p.relative_to(jobs)) for p in jobs.rglob('*')
                                  if p.name.startswith(('.fetch-', '.collection-'))])


def cached_variants(root, repo, count):
    tools, service, env = setup(root, repo)
    cache = root / 'controller/build/datasets'
    cache.mkdir(parents=True)
    references = root / 'references'
    references.mkdir()
    requests = root / 'process-results.jsonl'
    accepted = refused = 0
    sample = []
    seen_paths = []
    with Peak(cache) as peak:
        for i in range(count):
            reference = object_bundle(service, {'inputs/shared.u64': SHARED,
                                               'inputs/schema.json': json.dumps({'variant': i}).encode()})
            path = references / f'input-{i}.json'
            path.write_text(json.dumps(reference))
            result = call([sys.executable, str(tools / 'artifacts.py'), 'input', str(path)], env, requests)
            accepted += result.returncode == 0
            refused += result.returncode != 0
            if result.returncode == 0:
                dest = Path(result.stdout.strip())
                assert dest.is_relative_to(cache)
                assert (dest / 'inputs/shared.u64').read_bytes() == SHARED
                assert json.loads((dest / 'inputs/schema.json').read_text()) == {'variant': i}
                seen_paths.append(dest)
            if i + 1 in {1, 10, count}:
                sample.append({'variants': i + 1, **disk_walk(cache)})
        # This deliberately overlaps a full cached artifact with a selected view.
        selected = call([sys.executable, str(tools / 'artifacts.py'), 'input',
                         str(references / 'input-0.json'), '--file', 'inputs/shared.u64'], env, requests)
        assert selected.returncode == 0, selected.stderr
        assert (Path(selected.stdout.strip()) / 'inputs/shared.u64').read_bytes() == SHARED
        before = (service / 'requests.jsonl').read_text().count('\n')
        hit = call([sys.executable, str(tools / 'artifacts.py'), 'input',
                    str(references / 'input-0.json')], env, requests)
        assert hit.returncode == 0, hit.stderr
        after = (service / 'requests.jsonl').read_text().count('\n')
        assert before == after, 'warm reuse unexpectedly fetched bytes'
        changed = b'LOCAL INPUT EDIT' + SHARED[len(b'LOCAL INPUT EDIT'):]
        first = seen_paths[0] / 'inputs/shared.u64'
        first.write_bytes(changed)
        bad = call([sys.executable, str(tools / 'artifacts.py'), 'input',
                    str(references / 'input-0.json')], env, requests)
        assert bad.returncode != 0, 'edited cached input was accepted silently'
        assert first.read_bytes() == changed, 'edited input was overwritten or removed'
    final = disk_walk(cache)
    return dict(case='provisional-cached-input-variants', variants=count, accepted=accepted,
                refused=refused, configured_budget_bytes=MIB,
                independently_unique_payload_bytes=len(SHARED) + sum(len(json.dumps({'variant': i}).encode()) for i in range(count)),
                shared_member_copies=len(seen_paths) + 1,
                final=final, observed_peak_allocated_bytes=peak.maximum,
                samples=sample, bounded=peak.maximum <= MIB,
                warm_cache_uses_network=False,
                changed_cached_input_detected_and_preserved=True,
                leftover_staging=[str(p.relative_to(cache)) for p in cache.rglob('*')
                                  if p.name.startswith(('.fetch-', '.input-'))])


def candidate_workload(root, candidate, count, budget, expected_cut=None):
    root.mkdir(parents=True, exist_ok=False)
    # Freeze the tested candidate so an edit during the workload cannot change it.
    program = root / 'candidate.py'
    program.write_bytes(candidate.read_bytes())
    if expected_cut and digest(program.read_bytes()) != expected_cut:
        raise AssertionError('candidate source differs from requested frozen cut')
    store, backend = root / 'managed', root / 'backend'
    backend.mkdir()
    requests = root / 'process-results.jsonl'
    env = os.environ.copy()
    expected = {}
    accepted = refused = 0
    samples = []
    unexpected_errors = []

    def source(data):
        checksum = digest(data)
        p = backend / checksum
        if not p.exists():
            p.write_bytes(data)
        return p

    def invoke(operation, key=None, data=None, lease=None):
        nonlocal accepted, refused
        argv = [sys.executable, str(program), '--root', str(store), '--budget', str(budget), operation]
        if key is not None:
            argv.append(key)
        if data is not None:
            argv.append(str(source(data)))
            if operation == 'fetch':
                argv += ['--sha256', digest(data)]
        if lease:
            argv += ['--lease', lease]
        result = call(argv, env, requests)
        if result.returncode:
            refused += 1
            if not any(word in (result.stderr + result.stdout).lower()
                       for word in ['budget', 'capacity', 'quota', 'space']):
                unexpected_errors.append({'argv': argv, 'stderr': result.stderr, 'stdout': result.stdout})
            return None
        accepted += 1
        response = json.loads(result.stdout)
        if operation in {'fetch', 'put', 'get'}:
            path = Path(response['path'])
            assert path.is_relative_to(store), ('escaped managed root', path)
            wanted = data if data is not None else expected[key]
            assert path.read_bytes() == wanted, ('acknowledged incorrect bytes', key)
            expected[key] = wanted
        return response

    with Peak(store) as peak:
        assert invoke('fetch', 'active-input', SHARED, 'active-reader') is not None
        unique = b'UNARCHIVED-UNIQUE\n' * 384
        assert invoke('put', 'local-unique', unique) is not None
        assert invoke('fetch', 'shared-source', SOURCE) is not None
        shared_inode_counts = []
        for i in range(count):
            # This represents selecting one member, then a complete multi-member
            # result; all job labels refer to the same input and source bytes.
            invoke('fetch', f'job-{i:04}.input', SHARED)
            if i % 5 == 0:
                invoke('fetch', f'job-{i:04}.source', SOURCE)
            result = json.dumps({'job': i, 'answer': 42}).encode().ljust(512, b' ')
            invoke('fetch', f'job-{i:04}.result', result)
            if i + 1 in {1, 10, 50, 100, count}:
                samples.append({'jobs': i + 1, **disk_walk(store)})
                seen = set()
                for p in store.rglob('*'):
                    if p.is_file() and not p.is_symlink() and p.stat().st_size == len(SHARED):
                        if digest(p.read_bytes()) == digest(SHARED):
                            s = p.stat()
                            seen.add((s.st_dev, s.st_ino))
                shared_inode_counts.append({'jobs': i + 1, 'shared_payload_inodes': len(seen)})
                assert len(seen) == 1, ('same input accumulated independent payload copies', seen)
        # All invocations are fresh external processes; these checks therefore
        # also challenge restart reconciliation and protected-object retention.
        assert invoke('get', 'active-input') is not None, 'active leased input disappeared'
        assert invoke('get', 'local-unique') is not None, 'acknowledged unarchived data disappeared'
        for i in [0, count // 2, count - 1]:
            data = json.dumps({'job': i, 'answer': 42}).encode().ljust(512, b' ')
            if f'job-{i:04}.result' in expected:
                invoke('get', f'job-{i:04}.result')
            invoke('fetch', f'job-{i:04}.result', data)

        # Independent processes contend for a nearly full store; inspect actual
        # files throughout rather than adding their reported reservations.
        commands = []
        for i in range(16):
            data = (f'CONCURRENT-{i}\n'.encode() * 4096)[:32768]
            commands.append((f'concurrent-{i}', data))
        def concurrent(item):
            key, data = item
            argv = [sys.executable, str(program), '--root', str(store), '--budget', str(budget),
                    'fetch', key, str(source(data)), '--sha256', digest(data), '--lease', 'reader-' + key]
            result = call(argv, env, requests)
            if result.returncode == 0:
                response = json.loads(result.stdout)
                assert Path(response['path']).read_bytes() == data, key
                released = call([sys.executable, str(program), '--root', str(store), '--budget', str(budget),
                                 'release', 'reader-' + key], env, requests)
                assert released.returncode == 0, released.stderr
            elif not any(word in (result.stderr + result.stdout).lower()
                         for word in ['budget', 'capacity', 'quota', 'space']):
                unexpected_errors.append({'argv': argv, 'stderr': result.stderr, 'stdout': result.stdout})
            return result.returncode
        with ThreadPoolExecutor(max_workers=4) as pool:
            concurrent_codes = list(pool.map(concurrent, commands))
        assert invoke('get', 'active-input') is not None
        assert invoke('get', 'local-unique') is not None
    final = disk_walk(store)
    assert accepted > 3, 'refusing the entire useful workload is not a working cache'
    assert not unexpected_errors, unexpected_errors
    return dict(case='candidate-repeated-mixed-jobs', jobs=count, accepted_operations=accepted,
                refused_operations=refused, unexpected_errors=unexpected_errors,
                concurrent_codes=concurrent_codes, configured_budget_bytes=budget,
                shared_payload_inode_samples=shared_inode_counts, final=final,
                observed_peak_allocated_bytes=peak.maximum, samples=samples,
                bounded=peak.maximum <= budget,
                acknowledged_unique_and_active_bytes_preserved=True,
                candidate_sha256=digest(program.read_bytes()))


def candidate_pressure(root, candidate, count, budget, expected_cut=None):
    root.mkdir(parents=True, exist_ok=False)
    program = root / 'candidate.py'
    program.write_bytes(candidate.read_bytes())
    if expected_cut and digest(program.read_bytes()) != expected_cut:
        raise AssertionError('candidate source differs from requested frozen cut')
    store, backend = root / 'managed', root / 'backend'
    backend.mkdir()
    requests = root / 'process-results.jsonl'
    env = os.environ.copy()
    source = backend / 'shared'
    source.write_bytes(SHARED)
    unique = backend / 'unique'
    unique_bytes = b'ACKNOWLEDGED UNIQUE VALUE\n' * 512
    unique.write_bytes(unique_bytes)

    def run(operation, *args):
        return call([sys.executable, str(program), '--root', str(store), '--budget', str(budget),
                     operation, *map(str, args)], env, requests)

    def checked(result, wanted):
        assert result.returncode == 0, result.stderr
        path = Path(json.loads(result.stdout)['path'])
        assert path.read_bytes() == wanted
        return path

    accepted = refused = 0
    with Peak(store) as peak:
        active = checked(run('fetch', 'active', source, '--sha256', digest(SHARED), '--lease', 'live-reader'), SHARED)
        local = checked(run('put', 'unique', unique), unique_bytes)
        # Force alias metadata, rather than payload bytes, to exhaust capacity.
        for i in range(count):
            label = ('alias-' + str(i) + '-').ljust(256, 'x')
            result = run('fetch', label, source, '--sha256', digest(SHARED))
            if result.returncode:
                refused += 1
                assert any(s in result.stderr.lower() for s in ['capacity', 'quota', 'budget', 'space']), result.stderr
            else:
                accepted += 1
                checked(result, SHARED)
        assert accepted > 0 and refused > 0, 'metadata-pressure scenario did not reach a consequential capacity boundary'
        checked(run('get', 'active'), SHARED)
        checked(run('get', 'unique'), unique_bytes)
        assert active.read_bytes() == SHARED
        assert local.read_bytes() == unique_bytes
        # A reader's changed unarchived bytes must not be discarded or silently
        # accepted under the previously acknowledged digest.
        changed = b'LOCAL EDIT\n' + unique_bytes[len(b'LOCAL EDIT\n'):]
        local.chmod(0o600)  # Deliberate owner mutation in this isolated fixture.
        local.write_bytes(changed)
        bad = run('get', 'unique')
        assert bad.returncode != 0, 'changed unique content was silently accepted'
        assert local.read_bytes() == changed, 'changed unique content was lost'
        for i in range(12):
            data = (f'pressure-{i}\n'.encode() * 8192)[:32768]
            p = backend / f'new-{i}'
            p.write_bytes(data)
            run('fetch', 'new-' + str(i), p, '--sha256', digest(data))
        assert local.read_bytes() == changed, 'pressure removed unarchived local edits'
        checked(run('get', 'active'), SHARED)
    return dict(case='candidate-metadata-pressure-and-local-edit', attempted_aliases=count,
                accepted_aliases=accepted, capacity_refusals=refused,
                configured_budget_bytes=budget, observed_peak_allocated_bytes=peak.maximum,
                final=disk_walk(store), bounded=peak.maximum <= budget,
                active_lease_survived_capacity_pressure=True,
                acknowledged_unique_bytes_survived_capacity_pressure=True,
                changed_unarchived_bytes_detected_and_preserved=True,
                candidate_sha256=digest(program.read_bytes()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['legacy-workers', 'cached-variants', 'candidate', 'candidate-pressure'])
    parser.add_argument('--output', type=Path, required=True, help='new isolated fixture root (never an existing cache)')
    parser.add_argument('--repo', type=Path, default=REPO)
    parser.add_argument('--jobs', type=int, default=200)
    parser.add_argument('--variants', type=int, default=40)
    parser.add_argument('--candidate', type=Path, default=REPO / 'workbench/spikes/storage-lifecycle/store.py')
    parser.add_argument('--expected-candidate-sha256', help='refuse to test a different source cut')
    parser.add_argument('--budget', type=int, default=MIB)
    parser.add_argument('--expect-bounded', action='store_true', help='exit nonzero when independent accounting exceeds quota')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 300 or not 1 <= args.variants <= 100:
        parser.error('keep fixtures bounded: jobs 1..300, variants 1..100')
    if not 0 < args.budget <= 8 * MIB:
        parser.error('candidate budget must be positive and at most 8 MiB')
    started = time.monotonic()
    harness_sha256 = digest(Path(__file__).read_bytes())
    if args.mode == 'candidate':
        result = candidate_workload(args.output.resolve(), args.candidate.resolve(), args.jobs, args.budget,
                                    args.expected_candidate_sha256)
    elif args.mode == 'candidate-pressure':
        result = candidate_pressure(args.output.resolve(), args.candidate.resolve(), args.jobs, args.budget,
                                   args.expected_candidate_sha256)
    else:
        action = legacy_workers if args.mode == 'legacy-workers' else cached_variants
        result = action(args.output.resolve(), args.repo.resolve(), args.jobs if args.mode == 'legacy-workers' else args.variants)
    result['seconds'] = time.monotonic() - started
    result['harness_sha256'] = harness_sha256
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 1 if args.expect_bounded and not result['bounded'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
