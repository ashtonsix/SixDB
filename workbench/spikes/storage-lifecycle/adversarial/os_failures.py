#!/usr/bin/env python3
"""External-process storage attacks on a private 16 MiB tmpfs; never real caches.

Linux: python3 .../os_failures.py --backend legacy|candidate|both
The local fake aws executable reads fixture bytes only. No network is used.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / 'workbench/tools'
STORE = ROOT / 'workbench/spikes/storage-lifecycle/store.py'
MIB = 1024 ** 2
LIMIT = 16 * MIB


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def allocated(root):
    seen = set()
    total = 0
    for parent, children, names in os.walk(root, followlinks=False):
        children[:] = [n for n in children if not (Path(parent) / n).is_symlink()]
        for name in names:
            p = Path(parent) / name
            try:
                st = p.lstat()
            except FileNotFoundError:
                continue
            if not p.is_symlink() and (st.st_dev, st.st_ino) not in seen:
                seen.add((st.st_dev, st.st_ino))
                total += st.st_blocks * 512
    return total


def wait_for(predicate, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.005)
    raise AssertionError('external process did not reach the requested event')


def terminate(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGKILL)
    return process.communicate(timeout=10)


def stopped(process):
    try:
        return Path(f'/proc/{process.pid}/stat').read_text().split(') ', 1)[1].startswith('T')
    except FileNotFoundError:
        return False


class Peak:
    def __init__(self, root):
        self.root, self.peak = root, 0
        self.stop = threading.Event()

    def sample(self):
        while not self.stop.is_set():
            self.peak = max(self.peak, allocated(self.root))
            self.stop.wait(.001)

    def __enter__(self):
        self.thread = threading.Thread(target=self.sample, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join()
        self.peak = max(self.peak, allocated(self.root))


def spawn(argv, *, env):
    return subprocess.Popen(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, start_new_session=True)


def checked(argv, *, env, success=True):
    result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=20)
    if success and result.returncode:
        raise AssertionError(f'command failed: {argv}\n{result.stderr[-3000:]}')
    return result


def fill_filesystem(path):
    # This path is on our private mount. The mount itself is bounded to 16 MiB.
    count = 0
    with path.open('wb', buffering=0) as stream:
        while True:
            try:
                count += stream.write(b'F' * 65536)
            except OSError as error:
                if error.errno != 28:
                    raise
                return count


class Harness:
    def __init__(self, fixtures, mount, candidate):
        self.fixtures, self.mount, self.candidate = fixtures, mount, candidate
        self.results = []
        self.source_hashes = self.fingerprint()
        self.bin = fixtures / 'bin'
        self.bin.mkdir()
        aws = self.bin / 'aws'
        aws.write_text('''#!/usr/bin/env python3
import json,os,sys,time
from pathlib import Path
args=sys.argv[1:]
assert args[:2]==['s3api','get-object'], args
destination=Path(args[args.index('--key')+2])
threshold=int(os.environ.get('ATTACK_PAUSE_AFTER','0'))
paused=False
with open(os.environ['FAKE_AWS_SOURCE'],'rb') as source, destination.open('wb',buffering=0) as target:
 copied=0
 while data:=source.read(65536):
  target.write(data);copied+=len(data)
  if threshold and copied>=threshold and not paused:
   os.fsync(target.fileno())
   Path(os.environ['ATTACK_EVENT']).write_text(json.dumps({'path':str(destination),'bytes':copied}))
   deadline=time.monotonic()+15
   while not Path(os.environ['ATTACK_GO']).exists():
    if time.monotonic()>deadline:raise RuntimeError('fixture controller timeout')
    time.sleep(.005)
   paused=True
print('{}')
''')
        aws.chmod(0o755)
        self.env = os.environ | {'PATH': str(self.bin) + os.pathsep + os.environ['PATH'],
            'PYTHONPATH': str(TOOLS), 'SIXDB_MIN_FREE_GIB': '0',
            'XDG_CACHE_HOME': str(mount / 'controller-cache'),
            'SIXDB_WORKER_CACHE_GIB': str(1 / 1024)}

    def fingerprint(self):
        paths = [TOOLS / 'artifacts.py', TOOLS / 'storage.py', TOOLS / 'worker_cache.py', self.candidate]
        return {str(p.relative_to(ROOT)): sha(p) for p in paths if p.is_file() and p.is_relative_to(ROOT)}

    def case(self, name, fn):
        with tempfile.TemporaryDirectory(prefix=name + '-', dir=self.mount) as directory:
            start = time.monotonic()
            before = self.fingerprint()
            try:
                observation = fn(Path(directory))
                assert self.fingerprint() == before, 'implementation source changed during the case'
                self.results.append({'case': name, 'status': 'observed', **observation,
                                     'seconds': round(time.monotonic() - start, 3)})
            except Exception as error:
                self.results.append({'case': name, 'status': 'test-failed',
                                     'error': f'{type(error).__name__}: {error}',
                                     'seconds': round(time.monotonic() - start, 3)})
            print(json.dumps(self.results[-1]), flush=True)

    def bundle(self, label, data, member='data.csv'):
        path = self.fixtures / (label + '.tar.gz')
        with tarfile.open(path, 'w:gz') as archive:
            info = tarfile.TarInfo(member)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        reference = {'format': 1, 'kind': 'files', 'bucket': 'offline', 'key': label,
                     'region': 'none', 'sha256': sha(path), 'bytes': path.stat().st_size, 'files': 1}
        ref = self.fixtures / (label + '.json')
        ref.write_text(json.dumps(reference))
        return path, ref

    def legacy_command(self, job, reference):
        code = ('import json,sys;from pathlib import Path;import worker_cache;'
                'worker_cache.collect(Path(sys.argv[1]),json.loads(Path(sys.argv[2]).read_text()))')
        return [sys.executable, '-c', code, str(job), str(reference)]

    def legacy_killed_staging(self, root):
        data = random.Random(7).randbytes(4 * MIB)
        bundle, reference = self.bundle('killed', data)
        job = root / 'job'
        (job / 'results').mkdir(parents=True)
        notes = job / 'results/unique-notes'
        notes.write_text('must survive every interrupted collection')
        digest = sha(notes)
        for i in range(5):
            event, go = self.fixtures / f'kill-{i}.event', self.fixtures / f'kill-{i}.go'
            env = self.env | {'FAKE_AWS_SOURCE': str(bundle), 'ATTACK_PAUSE_AFTER': str(2 * MIB),
                              'ATTACK_EVENT': str(event), 'ATTACK_GO': str(go)}
            p = spawn(self.legacy_command(job, reference), env=env)
            try:
                wait_for(event.exists)
            finally:
                terminate(p)
        abandoned = list(job.glob('.collection-*/.fetch-*/bundle.tar.gz'))
        abandoned_bytes = sum(p.stat().st_size for p in abandoned)
        result = checked(self.legacy_command(job, reference),
                         env=self.env | {'FAKE_AWS_SOURCE': str(bundle)}, success=False)
        assert sha(notes) == digest
        assert not (job / 'results/data.csv').exists()
        assert len(abandoned) == 5 and abandoned_bytes == 10 * MIB
        assert result.returncode != 0
        assert all(p.exists() for p in abandoned)
        return {'counterexample': True, 'abandoned_downloads': len(abandoned),
                'abandoned_bytes': abandoned_bytes, 'normal_restart_succeeds': False,
                'unique_notes_unchanged': True, 'restart_error': result.stderr[-900:]}

    def legacy_real_enospc(self, root):
        bundle, reference = self.bundle('enospc', random.Random(8).randbytes(4 * MIB))
        job = root / 'job'
        (job / 'results').mkdir(parents=True)
        notes = job / 'results/unique-notes'
        notes.write_text('accepted earlier work')
        digest = sha(notes)
        event, go = self.fixtures / 'enospc.event', self.fixtures / 'enospc.go'
        env = self.env | {'FAKE_AWS_SOURCE': str(bundle), 'ATTACK_PAUSE_AFTER': str(MIB),
                          'ATTACK_EVENT': str(event), 'ATTACK_GO': str(go)}
        p = spawn(self.legacy_command(job, reference), env=env)
        filler = root / 'external-writer'
        try:
            wait_for(event.exists)
            filled = fill_filesystem(filler)
            go.touch()
            stdout, stderr = p.communicate(timeout=15)
            assert p.returncode != 0 and 'No space left on device' in stderr
            assert sha(notes) == digest
            assert not (job / 'results/data.csv').exists()
            assert not list(job.glob('.collection-*'))
            return {'actual_kernel_enospc': True, 'filler_bytes': filled,
                    'partial_result_published': False, 'unique_notes_unchanged': True,
                    'graceful_failure_staging_removed': True}
        finally:
            terminate(p)
            filler.unlink(missing_ok=True)

    def legacy_budget(self, root):
        bundle, reference = self.bundle('budget', b'known result\n' * (MIB // 13))
        processes = []
        for i in range(6):
            job = root / f'job-{i}'
            job.mkdir()
            processes.append((job, spawn(self.legacy_command(job, reference),
                              env=self.env | {'FAKE_AWS_SOURCE': str(bundle)})))
        for job, process in processes:
            output, error = process.communicate(timeout=20)
            assert process.returncode == 0, error
            assert sha(job / 'results/data.csv') == hashlib.sha256(b'known result\n' * (MIB // 13)).hexdigest()
        size = allocated(root)
        assert size > MIB
        return {'counterexample': True, 'nominal_worker_budget': MIB,
                'successful_concurrent_collections': len(processes), 'actual_allocated_bytes': size,
                'budget_enforced_at_admission': False}

    def legacy_paths(self, root):
        outside = root / 'outside'
        outside.mkdir()
        sentinel = outside / 'sentinel'
        sentinel.write_text('independent untouched witness')
        digest = sha(sentinel)
        bundle, reference = self.bundle('unsafe', b'escape', '../outside/sentinel')
        job = root / 'job'
        job.mkdir()
        result = checked(self.legacy_command(job, reference),
                         env=self.env | {'FAKE_AWS_SOURCE': str(bundle)}, success=False)
        assert result.returncode != 0 and sha(sentinel) == digest
        return {'unsafe_archive_rejected': True, 'outside_witness_unchanged': True}

    def command(self, root, budget, *args):
        return [sys.executable, str(self.candidate), '--root', str(root), '--budget', str(budget), *map(str, args)]

    def source(self, name, data):
        path = self.fixtures / name
        path.write_bytes(data)
        return path

    def candidate_concurrent(self, root):
        cache, budget = root / 'cache', 2 * MIB
        sources = [self.source(f'concurrent-{i}', bytes([i + 1]) * (512 * 1024)) for i in range(6)]
        with Peak(cache) as peak:
            processes = [(str(i), path, spawn(self.command(cache, budget, 'put', str(i), path), env=self.env))
                         for i, path in enumerate(sources)]
            accepted = []
            for key, source, process in processes:
                output, error = process.communicate(timeout=20)
                if process.returncode == 0:
                    accepted.append((key, source))
            assert accepted and len(accepted) < len(processes)
            for key, source in accepted:
                answer = checked(self.command(cache, budget, 'get', key), env=self.env)
                saved = Path(json.loads(answer.stdout)['path'])
                assert saved.is_relative_to(cache) and sha(saved) == sha(source)
        assert peak.peak <= budget, (peak.peak, budget)
        return {'accepted': len(accepted), 'refused': len(processes) - len(accepted),
                'budget_bytes': budget, 'independent_peak_allocated_bytes': peak.peak,
                'acknowledged_content_preserved': True}

    def candidate_crashes(self, root):
        source = self.source('crash-source', b'C' * MIB)
        observations = []
        for point in ['after-stage', 'after-object', 'after-catalog']:
            cache = root / point
            p = spawn(self.command(cache, 4 * MIB, 'put', 'value', source),
                      env=self.env | {'SIXDB_STORAGE_TEST_STOP': point})
            try:
                wait_for(lambda: stopped(p) or p.poll() is not None)
                assert stopped(p), (point, p.communicate(timeout=10))
            finally:
                terminate(p)
            checked(self.command(cache, 4 * MIB, 'stats'), env=self.env)
            retry = checked(self.command(cache, 4 * MIB, 'put', 'value', source), env=self.env)
            answer = checked(self.command(cache, 4 * MIB, 'get', 'value'), env=self.env)
            assert sha(Path(json.loads(answer.stdout)['path'])) == sha(source)
            assert allocated(cache) <= 4 * MIB
            observations.append({'point': point, 'restart_and_retry_succeeded': True,
                                 'allocated_after_restart': allocated(cache)})
        return {'stops': observations}

    def candidate_arbitrary_kills(self, root):
        source = self.source('arbitrary-kill-source', b'K' * (512 * 1024))
        delays = [.0001, .003, .008, .015, .025, .032, .04, .05]
        killed = acknowledged = 0
        for i in range(32):
            with tempfile.TemporaryDirectory(prefix='window-', dir=root) as directory:
                cache = Path(directory) / 'cache'
                p = spawn(self.command(cache, 2 * MIB, 'put', 'value', source), env=self.env)
                time.sleep(delays[i % len(delays)])
                if p.poll() is None:
                    killed += 1
                    terminate(p)
                else:
                    output, error = p.communicate(timeout=10)
                    assert p.returncode == 0, error
                    acknowledged += 1
                checked(self.command(cache, 2 * MIB, 'stats'), env=self.env)
                checked(self.command(cache, 2 * MIB, 'put', 'value', source), env=self.env)
                answer = checked(self.command(cache, 2 * MIB, 'get', 'value'), env=self.env)
                assert sha(Path(json.loads(answer.stdout)['path'])) == sha(source)
                assert allocated(cache) <= 2 * MIB
        return {'arbitrary_process_kills': killed, 'completed_before_kill': acknowledged,
                'restarts_retries_and_content_checked': 32}

    def candidate_real_enospc(self, root):
        source = self.source('candidate-enospc-source', b'E' * (8 * MIB))
        cache, budget = root / 'cache', 12 * MIB
        p = spawn(self.command(cache, budget, 'put', 'new', source),
                  env=self.env | {'SIXDB_STORAGE_TEST_STOP': 'after-stage'})
        filler = root / 'external-writer'
        try:
            wait_for(lambda: stopped(p) or p.poll() is not None)
            assert stopped(p), p.communicate(timeout=10)
            filled = fill_filesystem(filler)
            os.killpg(p.pid, signal.SIGCONT)
            output, error = p.communicate(timeout=15)
            assert p.returncode != 0 and 'No space left on device' in error, (output, error)
            filler.unlink()
            checked(self.command(cache, budget, 'stats'), env=self.env)
            checked(self.command(cache, budget, 'put', 'new', source), env=self.env)
            answer = checked(self.command(cache, budget, 'get', 'new'), env=self.env)
            assert sha(Path(json.loads(answer.stdout)['path'])) == sha(source)
            return {'actual_kernel_enospc': True, 'filler_bytes': filled,
                    'restart_retry_content_verified': True, 'allocated_after_restart': allocated(cache)}
        finally:
            terminate(p)
            filler.unlink(missing_ok=True)

    def candidate_initial_enospc(self, root):
        cache, budget = root / 'cache', MIB
        cache.mkdir()
        filler = root / 'external-writer'
        try:
            filled = fill_filesystem(filler)
            initial = checked(self.command(cache, budget, 'stats'), env=self.env, success=False)
            assert initial.returncode != 0 and 'No space left on device' in initial.stderr
            filler.unlink()
            retry = checked(self.command(cache, budget, 'stats'), env=self.env, success=False)
            catalog = cache / 'catalog.json'
            assert retry.returncode == 0, (
                'initial ENOSPC left unrecoverable initialization state; '
                f'catalog_bytes={catalog.stat().st_size if catalog.exists() else None}; '
                f'retry={retry.stderr[-1500:]}')
            return {'actual_kernel_enospc_during_initialization': True, 'filler_bytes': filled,
                    'initialization_retry_succeeded': True}
        finally:
            filler.unlink(missing_ok=True)

    def candidate_recovery_and_lease(self, root):
        cache, budget = root / 'cache', MIB
        unique = self.source('unique', b'U' * (256 * 1024))
        old_bytes = b'A' * (384 * 1024)
        old = self.source('recoverable', old_bytes)
        new = self.source('replacement', b'N' * (640 * 1024))
        checked(self.command(cache, budget, 'put', 'unique', unique), env=self.env)
        old_get = checked(self.command(cache, budget, 'fetch', 'old', old,
                                      '--sha256', sha(old), '--lease', 'owner'), env=self.env)
        # Do not depend on fetch's optional output schema: get is the public lookup.
        answer = checked(self.command(cache, budget, 'get', 'old'), env=self.env)
        old_path = Path(json.loads(answer.stdout)['path'])
        refused = checked(self.command(cache, budget, 'fetch', 'new', new,
                                      '--sha256', sha(new)), env=self.env, success=False)
        assert refused.returncode != 0 and sha(old_path) == hashlib.sha256(old_bytes).hexdigest()
        checked(self.command(cache, budget, 'release', 'owner'), env=self.env)
        old.write_bytes(b'X' * len(old_bytes))
        refused = checked(self.command(cache, budget, 'fetch', 'new', new,
                                      '--sha256', sha(new)), env=self.env, success=False)
        assert refused.returncode != 0
        assert sha(old_path) == hashlib.sha256(old_bytes).hexdigest()
        old.write_bytes(old_bytes)
        checked(self.command(cache, budget, 'fetch', 'new', new, '--sha256', sha(new)), env=self.env)
        answer = checked(self.command(cache, budget, 'get', 'unique'), env=self.env)
        assert sha(Path(json.loads(answer.stdout)['path'])) == sha(unique)
        return {'leased_data_survived_pressure': True, 'corrupt_recovery_blocked_eviction': True,
                'unique_data_survived_eviction': True, 'released_valid_recovery_allowed_progress': True}

    def candidate_modified_object(self, root):
        cache, budget = root / 'cache', MIB
        source = self.source('original-managed', b'A' * (384 * 1024))
        new = self.source('managed-pressure', b'B' * (768 * 1024))
        checked(self.command(cache, budget, 'fetch', 'old', source, '--sha256', sha(source)), env=self.env)
        answer = checked(self.command(cache, budget, 'get', 'old'), env=self.env)
        managed = Path(json.loads(answer.stdout)['path'])
        changed = b'locally changed data\n' * (384 * 1024 // 20)
        managed.chmod(0o600)  # Read-only mode is not immutability against its owner.
        managed.write_bytes(changed)
        result = checked(self.command(cache, budget, 'fetch', 'new', new,
                                     '--sha256', sha(new)), env=self.env, success=False)
        assert managed.exists() and managed.read_bytes() == changed, result.stderr
        assert result.returncode != 0
        return {'externally_modified_managed_bytes_preserved': True, 'pressure_refused': True}

    def candidate_alias_metadata(self, root):
        cache, budget = root / 'cache', 256 * 1024
        source = self.source('alias-source', b'S' * (16 * 1024))
        accepted = []
        with Peak(cache) as peak:
            for i in range(512):
                key = f'alias{i:04d}-' + 'k' * 54
                result = checked(self.command(cache, budget, 'fetch', key, source,
                                              '--sha256', sha(source)), env=self.env, success=False)
                if result.returncode:
                    break
                accepted.append(key)
            assert accepted, 'candidate cannot admit the tiny starting fixture'
            assert len(accepted) < 512, 'unbounded alias metadata accepted'
            answer = checked(self.command(cache, budget, 'get', accepted[-1]), env=self.env)
            assert sha(Path(json.loads(answer.stdout)['path'])) == sha(source)
        assert peak.peak <= budget, (peak.peak, budget)
        return {'accepted_aliases': len(accepted), 'capacity_refused_more_aliases': True,
                'independent_peak_allocated_bytes': peak.peak, 'budget_bytes': budget,
                'last_acknowledged_data_preserved': True}

    def candidate_object_directory_symlink(self, root):
        cache, budget = root / 'cache', 2 * MIB
        source = self.source('symlink-initial', b'A' * 65536)
        second = self.source('symlink-second', b'B' * 65536)
        checked(self.command(cache, budget, 'put', 'first', source), env=self.env)
        answer = checked(self.command(cache, budget, 'get', 'first'), env=self.env)
        first = Path(json.loads(answer.stdout)['path'])
        objects = cache / first.relative_to(cache).parts[0]
        assert objects.is_relative_to(cache) and objects != cache
        outside = root / 'outside'
        outside.mkdir()
        witness = outside / 'witness'
        witness.write_text('outside write boundary')
        saved = root / 'saved-object-directory'
        objects.rename(saved)
        objects.symlink_to(outside, target_is_directory=True)
        result = checked(self.command(cache, budget, 'put', 'second', second), env=self.env, success=False)
        assert witness.read_text() == 'outside write boundary'
        assert set(p.name for p in outside.iterdir()) == {'witness'}, result.stderr
        assert result.returncode != 0
        assert sha(saved / first.name) == sha(source)
        return {'replaced_object_directory_rejected': True, 'outside_witness_unchanged': True,
                'no_object_written_outside_managed_root': True}

    def candidate_mid_operation_directory_swap(self, root):
        observations = []
        for which in ['staging', 'root']:
            case = root / which
            case.mkdir()
            cache, budget = case / 'cache', 2 * MIB
            accepted = self.source('swap-accepted-' + which, b'A' * 65536)
            incoming = self.source('swap-incoming-' + which, b'B' * (512 * 1024))
            checked(self.command(cache, budget, 'put', 'accepted', accepted), env=self.env)
            answer = checked(self.command(cache, budget, 'get', 'accepted'), env=self.env)
            acknowledged = Path(json.loads(answer.stdout)['path']).relative_to(cache)
            outside = case / 'outside'
            outside.mkdir()
            (outside / 'witness').write_text('outside boundary')
            p = spawn(self.command(cache, budget, 'put', 'incoming', incoming),
                      env=self.env | {'SIXDB_STORAGE_TEST_STOP': 'after-stage'})
            try:
                wait_for(lambda: stopped(p) or p.poll() is not None)
                assert stopped(p), p.communicate(timeout=10)
                attacked = cache / 'staging' if which == 'staging' else cache
                saved = case / ('saved-' + which)
                attacked.rename(saved)
                attacked.symlink_to(outside, target_is_directory=True)
                os.killpg(p.pid, signal.SIGCONT)
                output, error = p.communicate(timeout=15)
                assert p.returncode != 0, f'acknowledged after managed {which} path replacement: {output}'
                assert set(q.name for q in outside.iterdir()) == {'witness'}, error
                assert (outside / 'witness').read_text() == 'outside boundary'
                retained = cache / acknowledged if which == 'staging' else saved / acknowledged
                assert sha(retained) == sha(accepted)
                observations.append({'swapped': which, 'refused': True,
                                     'outside_unchanged': True, 'accepted_data_unchanged': True})
            finally:
                terminate(p)
        return {'directory_swaps_after_stage': observations}

    def run(self, backend):
        if backend in {'legacy', 'both'}:
            for name, case in [('legacy-sigkill-staging', self.legacy_killed_staging),
                               ('legacy-real-enospc', self.legacy_real_enospc),
                               ('legacy-concurrent-budget', self.legacy_budget),
                               ('legacy-unsafe-archive', self.legacy_paths)]:
                self.case(name, case)
        if backend in {'candidate', 'both'}:
            if not self.candidate.is_file():
                self.results.append({'case': 'candidate', 'status': 'unavailable', 'path': str(self.candidate)})
            else:
                for name, case in [('candidate-concurrent-admission', self.candidate_concurrent),
                                   ('candidate-sigkill-restart', self.candidate_crashes),
                                   ('candidate-arbitrary-kill-windows', self.candidate_arbitrary_kills),
                                   ('candidate-real-enospc', self.candidate_real_enospc),
                                   ('candidate-initial-enospc', self.candidate_initial_enospc),
                                   ('candidate-leases-recovery', self.candidate_recovery_and_lease),
                                   ('candidate-modified-object', self.candidate_modified_object),
                                   ('candidate-alias-metadata', self.candidate_alias_metadata),
                                   ('candidate-object-directory-symlink', self.candidate_object_directory_symlink),
                                   ('candidate-mid-operation-directory-swap', self.candidate_mid_operation_directory_swap)]:
                    self.case(name, case)
        return self.results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['legacy', 'candidate', 'both'], default='both')
    parser.add_argument('--candidate', type=Path, default=STORE)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/reviews/storage-20261024/adversarial-os/report.json')
    parser.add_argument('--namespace', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--fixtures', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.namespace:
        with tempfile.TemporaryDirectory(prefix='os-fixtures-', dir=args.output.parent) as temp:
            command = ['unshare', '--user', '--map-root-user', '--mount', sys.executable,
                       str(Path(__file__).resolve()), '--namespace', '--fixtures', temp,
                       '--backend', args.backend, '--candidate', str(args.candidate), '--output', str(args.output)]
            return subprocess.run(command, timeout=150).returncode
    subprocess.run(['mount', '--make-rprivate', '/'], check=True)
    mount = args.fixtures / 'private-tmpfs'
    mount.mkdir()
    subprocess.run(['mount', '-t', 'tmpfs', '-o', 'size=16m,nosuid,nodev', 'tmpfs', str(mount)], check=True)
    try:
        harness = Harness(args.fixtures, mount, args.candidate)
        results = harness.run(args.backend)
        report = {'filesystem': 'private user/mount namespace tmpfs', 'filesystem_limit_bytes': LIMIT,
                  'network_used': False, 'production_cache_modified': False, 'cases': results,
                  'implementation_sha256_before': harness.source_hashes,
                  'implementation_sha256_after': harness.fingerprint(),
                  'implementation_changed_during_run': harness.source_hashes != harness.fingerprint()}
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        return int(report['implementation_changed_during_run'] or
                   any(r['status'] in {'test-failed', 'unavailable'} for r in results))
    finally:
        subprocess.run(['umount', str(mount)], check=True)
        mount.rmdir()


if __name__ == '__main__':
    raise SystemExit(main())
