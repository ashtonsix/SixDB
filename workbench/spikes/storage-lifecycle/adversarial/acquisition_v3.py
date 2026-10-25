#!/usr/bin/env python3
"""Fresh black-box acquisition review. Run with Linux Python; no network used.

Only writes beneath build/reviews/storage-20261024/fresh-acquisition-v3.
The frozen pair is deliberately independent of later implementation changes.
"""
from __future__ import annotations

import gzip
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
import threading
import time

REPO = Path(__file__).resolve().parents[4]
BASE = REPO / 'build/reviews/storage-20261024/fresh-acquisition-v3'
FROZEN = BASE / 'frozen'
RUN = BASE / 'cases'
EXPECTED = {'store.py': '5df915ee47486391b449661b01945c3afc1183069dd117292be80322992c89bb',
            'acquire.py': 'ead261a602811b5c7d39063112997180dd78776ffd3631b3c78d4bd4ecc8dc5b'}
BUDGET = 384 * 1024
RESULTS = []
PEAKS = {}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def allocated(root):
    # Independent filesystem walk; never calls Store.used or CLI stats.
    total = 0
    for folder, dirs, files in os.walk(root):
        for path in [Path(folder), *[Path(folder) / name for name in files]]:
            try:
                total += path.lstat().st_blocks * 512
            except FileNotFoundError:
                pass  # Concurrent rename/unlink; later samples still check it.
    return total


def regular_bytes(root):
    return sum(p.stat().st_size for p in root.rglob('*') if p.is_file())


def record(name, **data):
    item = {'case': name, **data}
    RESULTS.append(item)
    print(json.dumps(item, sort_keys=True), flush=True)


def command(tool, root, operation, *args, budget=BUDGET, env=None):
    cmd = [sys.executable, str(FROZEN / tool), '--root', str(root), '--budget', str(budget), operation, *map(str, args)]
    return cmd, dict(os.environ, **(env or {}))


def invoke(tool, root, operation, *args, budget=BUDGET, env=None, expected=0):
    cmd, environ = command(tool, root, operation, *args, budget=budget, env=env)
    finished = threading.Event()
    def measure():
        while not finished.is_set():
            PEAKS[str(root)] = max(PEAKS.get(str(root), 0), allocated(root))
            finished.wait(.001)
    monitor = threading.Thread(target=measure)
    monitor.start()
    try:
        result = subprocess.run(cmd, env=environ, capture_output=True, text=True, timeout=20)
    finally:
        finished.set()
        monitor.join()
    PEAKS[str(root)] = max(PEAKS.get(str(root), 0), allocated(root))
    assert PEAKS[str(root)] <= budget, ('sampled allocation exceeds budget', root, PEAKS[str(root)], budget)
    assert result.returncode == expected, (cmd, result.returncode, result.stdout, result.stderr)
    if result.returncode == 0:
        return json.loads(result.stdout)
    assert not result.stdout, result.stdout
    return json.loads(result.stderr)


def fixture(name, members, *, raw=None, subdirectory=None):
    folder = RUN / 'fixtures'
    folder.mkdir(parents=True, exist_ok=True)
    if raw is None:
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode='w', format=tarfile.USTAR_FORMAT) as tar:
            for member, data in members:
                info = tarfile.TarInfo(member)
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
        raw = stream.getvalue()
    blob = gzip.compress(raw, mtime=0)
    archive = folder / (name + '.tar.gz')
    reference = folder / (name + '.json')
    archive.write_bytes(blob)
    ref = {'format': 1, 'sha256': sha(blob), 'bytes': len(blob), 'files': len(members)}
    if subdirectory is not None:
        ref['subdirectory'] = subdirectory
    reference.write_text(json.dumps(ref))
    return reference, archive


def mapping(members):
    return {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in members}


def verify_present(root, expected):
    objects = root / 'objects'
    for path in objects.iterdir():
        assert path.name in expected, ('unexpected object', path)
        assert path.read_bytes() == expected[path.name], ('incorrect object', path)
    return {'allocated': allocated(root), 'resident_payload_bytes': sum(p.stat().st_size for p in objects.iterdir()),
            'resident_objects': len(list(objects.iterdir()))}


def wait_until(test, timeout=10):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if test():
            return
        time.sleep(.01)
    raise AssertionError('condition timed out')


def repeated_workloads():
    root = RUN / 'repeat'
    rng = random.Random(331)
    common = rng.randbytes(16384)
    expected, peak, acknowledged, max_catalog = {}, 0, 0, 0
    jobs = 64
    for job in range(jobs):
        unique = rng.randbytes(16384)
        members = [('job/common', common), ('job/value', unique)]
        ref, archive = fixture('job-' + str(job), members, subdirectory='job')
        out = invoke('acquire.py', root, 'acquire', ref, archive)
        assert out['members'] == mapping([('common', common), ('value', unique)])
        assert 'paths' not in out
        expected.update({sha(data): data for _, data in members})
        for _ in range(2):
            again = invoke('acquire.py', root, 'acquire', ref, archive, '--file', 'common')
            assert again['members'] == mapping([('common', common)])
        acknowledged += 3 * len(common) + len(unique)
        stats = verify_present(root, expected)
        peak = max(peak, stats['allocated'])
        max_catalog = max(max_catalog, (root / 'catalog.json').stat().st_size)
        assert stats['allocated'] <= BUDGET
    catalog = json.loads((root / 'catalog.json').read_text())
    assert len([p for p in (root / 'objects').iterdir() if p.name == sha(common)]) == 1
    assert len(catalog['objects']) < jobs + 1, 'metadata history was not actually trimmed'
    record('repeated-changing-jobs', successful_jobs=jobs, independently_acknowledged_payload_bytes=acknowledged,
           expected_unique_payload_bytes=sum(map(len, expected.values())), peak_postoperation_allocation=peak,
           sampled_peak_operation_allocation=PEAKS[str(root)], successful_acquisitions=jobs * 3,
           max_catalog_bytes=max_catalog, catalog_history_trimmed=True,
           catalog_bytes=(root / 'catalog.json').stat().st_size, catalog_objects=len(catalog['objects']), **stats)


def manifest_classification_counterexample():
    root = RUN / 'manifest-counterexample'
    members = [('a', b'ordinary a'), ('b', b'ordinary b')]
    ref, archive = fixture('minimal-manifest', members)
    manifest = invoke('acquire.py', root, 'inspect', ref, archive)
    initial = invoke('acquire.py', root, 'acquire', ref, archive)
    assert set(initial['members']) == {'a', 'b'}
    manifest['members']['b']['rebuildable'] = True
    manifest_path = RUN / 'minimal-forged-manifest.json'
    manifest_path.write_text(json.dumps(manifest))
    archive.rename(archive.with_suffix('.offline'))
    result = invoke('acquire.py', root, 'acquire', ref, archive, '--manifest', manifest_path)
    assert result['cache_hit'] and set(result['members']) == {'a'}, result
    record('minimal-manifest-classification-counterexample', bug_reproduced=True,
           expected_default_members=['a', 'b'], actual_default_members=list(result['members']),
           omitted=result['omitted'], cache_hit=result['cache_hit'], archive_available=False,
           allocated=allocated(root))


def selection_and_manifest():
    root = RUN / 'selection'
    members = [('a', b'A' * 128), ('b', b'B' * 256), ('source.tar.gz', b'source'),
               ('compiler.bin', b'\x7fELF' + b'X' * (1024 * 1024 - 4))]
    ref, archive = fixture('selection', members)
    inspected = invoke('acquire.py', root, 'inspect', ref, archive)
    manifest = RUN / 'selection.manifest.json'
    manifest.write_text(json.dumps(inspected))
    out = invoke('acquire.py', root, 'acquire', ref, archive)
    assert out['members'] == mapping(members[:2])
    assert out['omitted'] == ['compiler.bin', 'source.tar.gz']
    slim = allocated(root)
    selected = invoke('acquire.py', root, 'acquire', ref, archive, '--file', 'source.tar.gz')
    assert selected['members'] == mapping([members[2]])
    full_root = RUN / 'full'
    full = invoke('acquire.py', full_root, 'acquire', ref, archive, '--full', budget=2 * 1024 * 1024)
    assert full['members'] == mapping(members)
    # The documented manifest argument accepts an ordinary size/hash map. It
    # has no classification hints; warm default acquisition should still omit
    # compiler output, just as the cold default acquisition did above.
    plain_manifest = RUN / 'plain-hashes.manifest.json'
    plain_manifest.write_text(json.dumps({'members': mapping(members)}))
    plain_warm = invoke('acquire.py', full_root, 'acquire', ref, archive, '--manifest', plain_manifest,
                        budget=2 * 1024 * 1024)
    assert plain_warm['cache_hit'] and 'compiler.bin' in plain_warm['members']
    offline = archive.with_suffix('.offline')
    archive.rename(offline)
    cached = invoke('acquire.py', root, 'acquire', ref, archive, '--manifest', manifest)
    assert cached['cache_hit'] and cached['members'] == mapping(members[:2])
    # Keep the exact member hash map but forge the unverified selection metadata.
    wrong = json.loads(manifest.read_text())
    wrong['members']['b']['rebuildable'] = True
    wrong_path = RUN / 'forged-classification.manifest.json'
    wrong_path.write_text(json.dumps(wrong))
    forged = invoke('acquire.py', root, 'acquire', ref, archive, '--manifest', wrong_path)
    classification_bug = forged['cache_hit'] and 'b' not in forged['members']
    archive = offline.rename(archive)
    stale = json.loads(manifest.read_text())
    stale['members']['b']['sha256'] = '0' * 64
    stale_path = RUN / 'wrong-digest.manifest.json'
    stale_path.write_text(json.dumps(stale))
    error = invoke('acquire.py', root, 'acquire', ref, archive, '--manifest', stale_path, '--file', 'a', expected=2)
    assert 'mismatch' in error['error']
    # Classification alone also overrides a live archive; it never gets scanned
    # on this warm path, even though the genuine scan would classify b as data.
    live_forged = invoke('acquire.py', root, 'acquire', ref, archive, '--manifest', wrong_path)
    assert live_forged['cache_hit'] and 'b' not in live_forged['members']
    record('selection-offline-manifest', slim_allocation=slim, full_allocation=allocated(full_root),
           offline_cache_hit=cached['cache_hit'], wrong_hash_rejected=True,
           plain_hash_manifest_default_includes_compiler=True,
           forged_rebuildable_omits_required_member=classification_bug, forged_result=forged['members'])


def invalid_archives():
    root = RUN / 'invalid'
    variants = {}
    def raw(entries):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode='w', format=tarfile.USTAR_FORMAT) as tar:
            for name, data, kind in entries:
                info = tarfile.TarInfo(name)
                info.type = kind
                info.linkname = 'target' if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE) else ''
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
        return stream.getvalue()
    good = ('a', b'good', tarfile.REGTYPE)
    for label, name, kind in [('traversal', '../outside', tarfile.REGTYPE),
                              ('absolute', '/outside', tarfile.REGTYPE),
                              ('symlink', 'hidden', tarfile.SYMTYPE),
                              ('hardlink', 'hidden', tarfile.LNKTYPE),
                              ('directory', 'hidden', tarfile.DIRTYPE),
                              ('sparse', 'hidden', tarfile.GNUTYPE_SPARSE),
                              ('duplicate', 'a', tarfile.REGTYPE),
                              ('parent-conflict', 'a/child', tarfile.REGTYPE)]:
        variants[label] = raw([good, (name, b'', kind)])
    base = bytearray(raw([good, ('hidden', b'X', tarfile.REGTYPE)]))
    padding = bytearray(base)
    padding[1537] = 1
    variants['nonzero-unrequested-padding'] = bytes(padding)
    trailer = bytearray(base)
    trailer[-1] = 1
    variants['nonzero-trailer'] = bytes(trailer)
    refused = []
    for label, data in variants.items():
        ref, archive = fixture('invalid-' + label, [('a', b'good'), ('hidden', b'')], raw=data)
        error = invoke('acquire.py', root, 'acquire', ref, archive, '--file', 'a', expected=2)
        assert list((root / 'objects').iterdir()) == []
        refused.append({'variant': label, 'reason': error['error']})
    # A wrong compressed identity must fail even when selected data is intact.
    ref, archive = fixture('identity', [('a', b'good'), ('hidden', b'bad')])
    corrupted = bytearray(archive.read_bytes())
    corrupted[-8] ^= 1
    archive.write_bytes(corrupted)
    error = invoke('acquire.py', root, 'acquire', ref, archive, '--file', 'a', expected=2)
    assert list((root / 'objects').iterdir()) == []
    record('invalid-unrequested-members', rejected=refused, corrupted_crc_rejected=error['error'], allocated=allocated(root))


def interrupted_publication():
    outcomes = []
    for phase in ['after-stage', 'after-object', 'after-catalog']:
        root = RUN / ('crash-' + phase)
        members = [('one', b'1' * 32768), ('two', b'2' * 16384)]
        ref, archive = fixture('crash-' + phase, members)
        cmd, environ = command('acquire.py', root, 'acquire', ref, archive, env={'SIXDB_STORAGE_TEST_STOP': phase})
        proc = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        wait_until(lambda: proc.poll() is not None or Path('/proc', str(proc.pid), 'status').read_text().split('State:')[1].strip().startswith('T'))
        assert proc.poll() is None, proc.communicate()
        stopped_alloc = allocated(root)
        proc.kill()
        stdout, stderr = proc.communicate(timeout=5)
        assert not stdout
        # A restart reconciles unacknowledged scratch before normal admission.
        invoke('store.py', root, 'stats')
        assert not list((root / 'staging').iterdir()) and not (root / 'pending.json').exists()
        out = invoke('acquire.py', root, 'acquire', ref, archive)
        assert out['members'] == mapping(members)
        stats = verify_present(root, {sha(data): data for _, data in members})
        outcomes.append({'phase': phase, 'stopped_allocation': stopped_alloc, 'stdout_before_kill': stdout, **stats})
    record('interrupted-publication', outcomes=outcomes)


def interrupted_streaming():
    root = RUN / 'crash-during-stream'
    rng = random.Random(99)
    members = [('one', rng.randbytes(65536)), ('two', rng.randbytes(32768))]
    ref, archive = fixture('crash-stream', members)
    # Fault injection only: execute frozen main unchanged, but stop the process
    # immediately after its first actual CheckedOutput.write reaches disk.
    injection = ('import sys,os,signal; sys.path.insert(0,' + repr(str(FROZEN)) + '); '
                 'import store,acquire; original=store.CheckedOutput.write; '
                 'defn="def paused(self,data):\\n original(self,data)\\n os.kill(os.getpid(),signal.SIGSTOP)"; '
                 'exec(defn.replace("\\\\n","\\n")); store.CheckedOutput.write=paused; '
                 'sys.exit(acquire.main())')
    cmd, environ = command('acquire.py', root, 'acquire', ref, archive)
    cmd = [sys.executable, '-c', injection, *cmd[2:]]
    proc = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    wait_until(lambda: proc.poll() is not None or Path('/proc', str(proc.pid), 'status').read_text().split('State:')[1].strip().startswith('T'))
    assert proc.poll() is None, proc.communicate()
    before = allocated(root)
    assert not list((root / 'objects').iterdir())
    proc.kill()
    stdout, stderr = proc.communicate(timeout=5)
    assert not stdout
    invoke('store.py', root, 'stats')
    assert not list((root / 'staging').iterdir())
    out = invoke('acquire.py', root, 'acquire', ref, archive)
    assert out['members'] == mapping(members)
    record('interrupted-streaming', first_write_allocation=before, no_partial_publication=True,
           **verify_present(root, {sha(data): data for _, data in members}))


def pin_unique_and_corrupt_recovery():
    root = RUN / 'pins'
    rng = random.Random(41)
    data = rng.randbytes(65536)
    ref, archive = fixture('pinned', [('pinned', data)])
    out = invoke('acquire.py', root, 'acquire', ref, archive, '--pin', 'owner')
    pinned = Path(out['paths']['pinned'])
    unique = RUN / 'unique.bin'
    unique.write_bytes(rng.randbytes(65536))
    unique_out = invoke('store.py', root, 'put', 'unique', unique)
    original = unique.read_bytes()
    unique.unlink()
    for index in range(8):
        r, a = fixture('pressure-' + str(index), [('new', rng.randbytes(32768))])
        invoke('acquire.py', root, 'acquire', r, a)
        assert pinned.read_bytes() == data
        assert Path(unique_out['path']).read_bytes() == original
        assert allocated(root) <= BUDGET
    invoke('store.py', root, 'release', 'owner')
    # Valid local bytes whose recovery disappears must not be evicted.
    archive.write_bytes(b'corrupt')
    for index in range(8, 16):
        r, a = fixture('pressure-' + str(index), [('new', rng.randbytes(32768))])
        invoke('acquire.py', root, 'acquire', r, a)
        assert pinned.read_bytes() == data
    assert Path(unique_out['path']).read_bytes() == original
    record('pins-unique-corrupt-recovery', pin_and_unique_preserved=True, allocated=allocated(root),
           acknowledged_unique_bytes=len(original), acknowledged_pinned_bytes=len(data))


def recovery_and_local_corruption():
    root = RUN / 'recovery-corruption'
    rng = random.Random(104)
    payload = rng.randbytes(65536)
    ref, archive = fixture('recover-corrupt', [('original', payload)])
    invoke('acquire.py', root, 'acquire', ref, archive)
    object_path = root / 'objects' / sha(payload)
    key = 'member:' + sha(payload)
    for index in range(8):
        if not object_path.exists():
            break
        r, a = fixture('recover-pressure-' + str(index), [('pressure', rng.randbytes(65536))])
        invoke('acquire.py', root, 'acquire', r, a)
    assert not object_path.exists(), 'fixture did not force recovery'
    original_archive = archive.read_bytes()
    corrupt = bytearray(original_archive)
    corrupt[-8] ^= 1
    archive.write_bytes(corrupt)
    error = invoke('store.py', root, 'get', key, expected=2)
    assert not object_path.exists()
    invoke('store.py', root, 'stats')
    assert not list((root / 'staging').iterdir())
    archive.write_bytes(original_archive)
    recovered = invoke('store.py', root, 'get', key)
    assert recovered['sha256'] == sha(payload) and object_path.read_bytes() == payload
    object_path.chmod(0o644)
    altered = b'X' + payload[1:]
    object_path.write_bytes(altered)
    local_error = invoke('store.py', root, 'get', key, expected=2)
    assert object_path.read_bytes() == altered
    record('recovery-local-corruption', corrupt_recovery_refused=error['error'], repaired_backend_recovers=True,
           changed_local_preserved_and_refused=local_error['error'], allocated=allocated(root))


def concurrent_and_scoped_reader():
    root = RUN / 'concurrent'
    rng = random.Random(10)
    common = rng.randbytes(32768)
    ref, archive = fixture('concurrent', [('a', common)])
    commands = [command('acquire.py', root, 'acquire', ref, archive)[0] for _ in range(10)]
    procs = [subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for cmd in commands]
    peaks = [0]
    for proc in procs:
        stdout, stderr = proc.communicate(timeout=20)
        assert proc.returncode == 0, stderr
        assert json.loads(stdout)['members'] == mapping([('a', common)])
        peaks[0] = max(peaks[0], allocated(root))
    assert len(list((root / 'objects').iterdir())) == 1
    marker = RUN / 'reader-ready'
    reader_script = ('import os,json,pathlib,time; '
                     'p=pathlib.Path(json.loads(os.environ["SIXDB_INPUTS_JSON"])["a"]); '
                     'pathlib.Path(' + repr(str(marker)) + ').write_text(str(os.getpid())); '
                     'time.sleep(1); assert p.read_bytes() == ' + repr(common) + '; ')
    # Keep argv tiny: the independent expected hash is enough for this reader.
    reader_script = ('import os,json,pathlib,time,hashlib; '
                     'p=pathlib.Path(json.loads(os.environ["SIXDB_INPUTS_JSON"])["a"]); '
                     'pathlib.Path(' + repr(str(marker)) + ').write_text(str(os.getpid())); '
                     'time.sleep(1); assert hashlib.sha256(p.read_bytes()).hexdigest() == ' + repr(sha(common)))
    cmd, environ = command('acquire.py', root, 'run', ref, archive, '--', sys.executable, '-c', reader_script)
    reader = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    wait_until(lambda: marker.exists() or reader.poll() is not None)
    if marker.exists():
        other_ref, other_archive = fixture('reader-pressure', [('b', rng.randbytes(65536))])
        cmd, environ = command('acquire.py', root, 'acquire', other_ref, other_archive)
        waiter = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(.15)
        assert waiter.poll() is None, 'another acquisition bypassed the live reader lock'
        reader_stdout, reader_stderr = reader.communicate(timeout=10)
        assert reader.returncode == 0, reader_stderr
        waiter_stdout, waiter_stderr = waiter.communicate(timeout=10)
        assert waiter.returncode == 0, waiter_stderr
        reader_result = 'consumer succeeded; competing acquisition blocked'
    else:
        reader_stdout, reader_stderr = reader.communicate(timeout=10)
        assert reader.returncode != 0
        reader_result = {'refused': reader.returncode, 'stderr': reader_stderr}
    record('concurrent-reader', concurrent_requests=len(procs), one_shared_object=True,
           reader=reader_result, allocated=allocated(root))


def killed_reader_scope():
    root = RUN / 'killed-reader'
    ref, archive = fixture('killed-reader', [('a', b'reader-input')])
    marker = RUN / 'descendant-ready'
    child_code = ('import os,pathlib,time; '
                  'pathlib.Path(' + repr(str(marker)) + ').write_text(str(os.getpid())); time.sleep(30)')
    parent_code = ('import subprocess,sys,time; '
                   'subprocess.Popen([sys.executable,"-c",' + repr(child_code) + '],close_fds=True); time.sleep(30)')
    cmd, environ = command('acquire.py', root, 'run', ref, archive, '--', sys.executable, '-c', parent_code)
    proc = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    wait_until(lambda: marker.exists() or proc.poll() is not None)
    if not marker.exists():
        stdout, stderr = proc.communicate(timeout=5)
        record('killed-reader-scope', unavailable=True, stderr=stderr)
        return
    # A namespace PID is not the host PID: identify the descendant by marker
    # pathname in /proc argv, rather than trusting its namespace-local getpid.
    def hosts():
        result = []
        for entry in Path('/proc').iterdir():
            if entry.name.isdigit():
                try:
                    argv = (entry / 'cmdline').read_bytes().split(b'\0')
                    if len(argv) >= 3 and argv[1] == b'-c' and argv[2].decode() == child_code:
                        result.append(int(entry.name))
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    pass
        return result
    descendants = hosts()
    assert descendants, 'could not independently identify descendant'
    proc.kill()
    stdout, stderr = proc.communicate(timeout=5)
    def alive(pid):
        try:
            state = Path('/proc', str(pid), 'status').read_text().split('State:')[1].strip()[0]
            return state != 'Z'
        except FileNotFoundError:
            return False
    wait_until(lambda: not any(map(alive, descendants)))
    invoke('store.py', root, 'stats')
    record('killed-reader-scope', host_descendant_pids=descendants, all_dead_before_subsequent_request=True,
           allocated=allocated(root))


def main():
    for name, expected in EXPECTED.items():
        assert sha((FROZEN / name).read_bytes()) == expected, 'frozen source changed'
    if RUN.exists():
        shutil.rmtree(RUN)
    RUN.mkdir(parents=True)
    tests = [manifest_classification_counterexample] if sys.argv[1:] == ['--case', 'manifest'] else [
        repeated_workloads, manifest_classification_counterexample, selection_and_manifest,
        invalid_archives, interrupted_publication, interrupted_streaming, pin_unique_and_corrupt_recovery,
        recovery_and_local_corruption, concurrent_and_scoped_reader, killed_reader_scope]
    for test in tests:
        test()
        assert regular_bytes(BASE) <= 16 * 1024 * 1024, 'review fixture ceiling exceeded'
        assert allocated(BASE) <= 16 * 1024 * 1024, 'review allocation ceiling exceeded'
    result = {'frozen_sha256': EXPECTED, 'cases': RESULTS, 'fixture_regular_bytes': regular_bytes(BASE),
              'fixture_allocated_bytes': allocated(BASE), 'sampled_peak_operation_allocation_by_root': PEAKS}
    (BASE / 'results.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'summary': 'complete', 'fixture_regular_bytes': result['fixture_regular_bytes'],
                      'fixture_allocated_bytes': result['fixture_allocated_bytes']}), flush=True)


if __name__ == '__main__':
    main()
