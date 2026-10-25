#!/usr/bin/env python3
"""Fresh external-process reliability cases; never import the store under test.

Linux invocation from the repository:
  python3 workbench/spikes/storage-lifecycle/adversarial/fresh_breaker.py
The host invocation must be prefixed with ``orb -m ubuntu``. All fixture writes
are confined to the ignored review directory. No actual cache is addressed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[4]
BASE = REPO / 'build/reviews/storage-20261024/fresh-breaker'
SOURCE = REPO / 'workbench/spikes/storage-lifecycle/store.py'
FROZEN = BASE / 'store-frozen.py'
RESULTS = []


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def allocated(root):
    total = 0
    for path in [root, *root.rglob('*')]:
        try:
            total += path.lstat().st_blocks * 512
        except FileNotFoundError:
            # A sampling observer can race atomic publication/cleanup.
            pass
    return total


def fixture(directory, name, length, value):
    path = directory / name
    path.write_bytes(bytes([value]) * length)
    return path


def command(root, budget, *args):
    return [sys.executable, str(FROZEN), '--root', str(root), '--budget', str(budget), *map(str, args)]


def call(root, budget, *args, ok=True, **kwargs):
    result = subprocess.run(command(root, budget, *args), capture_output=True, text=True,
                            timeout=10, **kwargs)
    if ok is True:
        assert result.returncode == 0, (args, result.returncode, result.stderr)
    elif ok is False:
        assert result.returncode != 0, (args, result.stdout)
    output = result.stdout or result.stderr
    try:
        value = json.loads(output)
    except ValueError:
        value = {'non_json_output': output[-1000:]}
    return result.returncode, value


def case(name, function):
    directory = BASE / name
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir()
    try:
        details = function(directory)
        record = {'case': name, 'result': 'pass', 'details': details}
    except Exception as error:
        record = {'case': name, 'result': 'FAIL', 'error': repr(error)}
    RESULTS.append(record)
    print(json.dumps(record), flush=True)


def assert_bytes(root, budget, key, expected):
    _, result = call(root, budget, 'get', key)
    assert Path(result['path']).read_bytes() == expected


def repeated(directory):
    root, budget = directory / 'store', 256 * 1024
    source = fixture(directory, 'shared', 16384, 21)
    for n in range(40):
        call(root, budget, 'fetch', f'key-{n}', source, '--sha256', sha(source))
        assert allocated(root) <= budget
    _, result = call(root, budget, 'stats')
    assert result['objects'] == 1 and result['keys'] == 40
    assert_bytes(root, budget, 'key-0', source.read_bytes())
    return result


def concurrent(directory):
    root, budget = directory / 'store', 196608
    sources = [fixture(directory, f'input-{n}', 12288, n + 1) for n in range(24)]
    processes = [subprocess.Popen(command(root, budget, 'put', str(n), source),
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                 for n, source in enumerate(sources)]
    observed_high_water = 0
    deadline = time.monotonic() + 15
    while any(process.poll() is None for process in processes):
        observed_high_water = max(observed_high_water, allocated(root))
        assert observed_high_water <= budget, (observed_high_water, budget)
        assert time.monotonic() < deadline, 'simultaneous CLI requests timed out'
        time.sleep(.001)
    acknowledged = []
    refused = 0
    for n, process in enumerate(processes):
        out, err = process.communicate(timeout=15)
        if process.returncode == 0:
            result = json.loads(out)
            assert Path(result['path']).read_bytes() == sources[n].read_bytes()
            acknowledged.append(n)
        else:
            assert process.returncode == 2 and 'capacity refused' in err, err
            refused += 1
    assert acknowledged and refused
    for n in acknowledged:
        assert_bytes(root, budget, str(n), sources[n].read_bytes())
    assert allocated(root) <= budget
    return {'acknowledged': acknowledged, 'refused': refused, 'allocated': allocated(root),
            'observed_high_water': observed_high_water, 'budget': budget}


def crash_points(directory):
    results = []
    for point in ['after-stage', 'after-object', 'after-catalog']:
        sub = directory / point
        sub.mkdir()
        root, budget = sub / 'store', 262144
        anchor = fixture(sub, 'anchor', 8192, 71)
        fresh = fixture(sub, 'new', 32768, 72)
        _, accepted = call(root, budget, 'put', 'anchor', anchor, '--lease', 'reader')
        env = dict(os.environ, SIXDB_STORAGE_TEST_STOP=point)
        process = subprocess.Popen(command(root, budget, 'put', 'new', fresh), env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 5
        stopped = False
        while time.monotonic() < deadline:
            pid, status = os.waitpid(process.pid, os.WNOHANG | os.WUNTRACED)
            if pid and os.WIFSTOPPED(status):
                stopped = True
                break
            time.sleep(.002)
        assert stopped, point
        process.kill()
        process.communicate(timeout=5)
        before_restart = allocated(root)
        assert before_restart <= budget
        _, stats = call(root, budget, 'stats')
        assert accepted['sha256'] in stats['leases']['reader']
        assert_bytes(root, budget, 'anchor', anchor.read_bytes())
        assert allocated(root) <= budget
        call(root, budget, 'put', 'new', fresh)
        assert_bytes(root, budget, 'new', fresh.read_bytes())
        results.append({'point': point, 'allocated_before_restart': before_restart,
                        'stats_after_restart': stats})
    return results


def pins_and_bad_recovery(directory):
    root, budget = directory / 'store', 196608
    source = fixture(directory, 'recoverable', 65536, 91)
    pressure = fixture(directory, 'pressure', 65536, 92)
    _, accepted = call(root, budget, 'fetch', 'a', source, '--sha256', sha(source), '--lease', 'reader')
    call(root, budget, 'put', 'b', pressure, ok=False)
    assert Path(accepted['path']).read_bytes() == source.read_bytes()
    call(root, budget, 'release', 'reader')
    source.write_bytes(b'corrupt')
    call(root, budget, 'put', 'b', pressure, ok=False)
    assert Path(accepted['path']).read_bytes() == bytes([91]) * 65536
    source.write_bytes(bytes([91]) * 65536)
    call(root, budget, 'fetch', 'b', pressure, '--sha256', sha(pressure))
    assert not Path(accepted['path']).exists()
    assert_bytes(root, budget, 'a', source.read_bytes())
    return {'pinned_and_corrupt_recovery_refused': True, 'verified_recovery_retry_succeeded': True}


def file_size_failure(directory):
    root, budget = directory / 'store', 262144
    anchor = fixture(directory, 'anchor', 8192, 104)
    fresh = fixture(directory, 'fresh', 16384, 105)
    call(root, budget, 'put', 'anchor', anchor, '--lease', 'reader')
    def limit():
        resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))
    rc, result = call(root, budget, 'put', 'fresh', fresh, ok=False, preexec_fn=limit)
    call(root, budget, 'stats')
    assert_bytes(root, budget, 'anchor', anchor.read_bytes())
    call(root, budget, 'put', 'fresh', fresh)
    assert allocated(root) <= budget
    return {'failed_writer_rc': rc, 'failed_writer_output': result, 'retry': 'accepted'}


def metadata_pressure(directory):
    root, budget = directory / 'store', 131072
    anchor = fixture(directory, 'anchor', 1, 107)
    _, accepted = call(root, budget, 'put', 'anchor', anchor)
    held = []
    refused = False
    for n in range(20):
        # A get lease may be longer than a put/fetch lease. Each command remains
        # below Linux's single-argument bound, while the catalog reaches capacity.
        owner = str(n) + '-' + 'x' * 5000
        rc, _ = call(root, budget, 'get', 'anchor', '--lease', owner, ok=None)
        if rc:
            refused = True
            break
        held.append(owner)
        assert allocated(root) <= budget
    assert refused and held
    assert Path(accepted['path']).read_bytes() == anchor.read_bytes()
    _, stats = call(root, budget, 'stats')
    assert set(stats['leases']) == set(held)
    def limit():
        resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))
    rc, diagnostic = call(root, budget, 'get', 'anchor', ok=False, preexec_fn=limit)
    _, after = call(root, budget, 'stats')
    assert after['leases'] == stats['leases']
    assert Path(accepted['path']).read_bytes() == anchor.read_bytes()
    for owner in held:
        call(root, budget, 'release', owner)
    assert_bytes(root, budget, 'anchor', anchor.read_bytes())
    return {'accepted_5000_byte_labels': len(held), 'next_label_refused': refused,
            'failed_metadata_writer_rc': rc, 'diagnostic': diagnostic,
            'all_leases_released_after_failure': True}


def changed_cache(directory):
    root, budget = directory / 'store', 196608
    source = fixture(directory, 'recovery', 65536, 109)
    pressure = fixture(directory, 'pressure', 65536, 110)
    _, accepted = call(root, budget, 'fetch', 'anchor', source, '--sha256', sha(source))
    cached = Path(accepted['path'])
    cached.chmod(0o644)
    changed = bytes([108]) * 65536
    cached.write_bytes(changed)
    call(root, budget, 'fetch', 'pressure', pressure, '--sha256', sha(pressure), ok=False)
    assert cached.read_bytes() == changed
    call(root, budget, 'get', 'anchor', ok=False)
    assert cached.read_bytes() == changed
    cached.write_bytes(source.read_bytes())
    cached.chmod(0o444)
    call(root, budget, 'fetch', 'pressure', pressure, '--sha256', sha(pressure))
    assert_bytes(root, budget, 'anchor', source.read_bytes())
    return {'changed_cached_fixture_preserved_under_pressure': True,
            'corrected_fixture_retry_succeeded': True}


def damaged_metadata(directory):
    results = []
    for name, damage in [('truncated-journal', b'{'),
                         ('traversal-journal', b'{"digest":"../../outside"}')]:
        sub = directory / name
        sub.mkdir()
        root, budget = sub / 'store', 262144
        anchor = fixture(sub, 'anchor', 8192, 111)
        _, accepted = call(root, budget, 'put', 'anchor', anchor)
        outside = fixture(sub, 'outside', 64, 112)
        (root / 'pending.json').write_bytes(damage)
        rc, diagnostic = call(root, budget, 'stats', ok=False)
        assert Path(accepted['path']).read_bytes() == anchor.read_bytes()
        assert outside.read_bytes() == bytes([112]) * 64
        (root / 'pending.json').unlink()
        assert_bytes(root, budget, 'anchor', anchor.read_bytes())
        results.append({'damage': name, 'rc': rc, 'diagnostic': diagnostic})
    return results


def unsafe_links(directory):
    results = []
    for target in ['objects', 'staging', 'catalog.json', 'pending.json', '.lock']:
        sub = directory / target.replace('.', '_')
        sub.mkdir()
        root, budget = sub / 'store', 262144
        anchor = fixture(sub, 'anchor', 8192, 114)
        _, accepted = call(root, budget, 'put', 'anchor', anchor)
        outside = sub / 'outside'
        outside.mkdir()
        sentinel = fixture(outside, 'sentinel', 64, 115)
        original = root / target
        backup = root / ('saved-' + target)
        if original.exists():
            original.rename(backup)
        original.symlink_to(outside if target in ['objects', 'staging'] else sentinel)
        call(root, budget, 'stats', ok=False)
        assert sentinel.read_bytes() == bytes([115]) * 64
        original.unlink()
        if backup.exists():
            backup.rename(original)
        assert_bytes(root, budget, 'anchor', anchor.read_bytes())
        results.append(target)
    return {'link_targets_refused': results}


def enospc_inner(directory):
    """Runs only after a bounded private tmpfs mount has been established."""
    results = []
    for free_blocks in [0, 1, 2, 9, 10]:
        sub = directory / f'free-{free_blocks}'
        sub.mkdir()
        root, budget = sub / 'store', 2 * 1024 * 1024
        source = fixture(sub, 'anchor', 32768, 121)
        new = fixture(sub, 'new', 32768, 122)
        _, accepted = call(root, budget, 'put', 'anchor', source, '--lease', 'reader')
        fill = sub / 'capacity-fixture'
        available = os.statvfs(directory).f_bavail
        assert 0 <= available - free_blocks <= 1024
        with fill.open('wb') as out:
            for _ in range(available - free_blocks):
                out.write(bytes(4096))
            out.flush()
        assert os.statvfs(directory).f_bavail == free_blocks
        rc, diagnostic = call(root, budget, 'put', 'new', new, ok=None)
        assert Path(accepted['path']).read_bytes() == source.read_bytes()
        debris = sorted(p.name for p in root.rglob('*'))
        fill.unlink()
        _, stats = call(root, budget, 'stats')
        assert accepted['sha256'] in stats['leases']['reader']
        assert_bytes(root, budget, 'anchor', source.read_bytes())
        call(root, budget, 'put', 'new', new)
        assert allocated(root) <= budget
        results.append({'free_blocks_before_request': free_blocks,
                        'request_rc': rc, 'diagnostic': diagnostic,
                        'files_before_restart': debris, 'retry': 'accepted'})
    assert any(r['request_rc'] != 0 for r in results)
    assert any(r['request_rc'] == 0 for r in results)
    return results


def enospc(directory):
    # Mount never escapes this child user/mount namespace. The whole filesystem,
    # including its capacity fixture, is at most 4 MiB. Kernel teardown unmounts.
    script = ('mount --make-rprivate / && '
              'mount -t tmpfs -o size=4m,nr_inodes=256 tmpfs "$1" && '
              'exec "$2" "$3" --enospc-inner "$1"')
    process = subprocess.run(['unshare', '--user', '--map-root-user', '--mount',
                              'sh', '-c', script, 'private-tmpfs', str(directory),
                              sys.executable, str(Path(__file__).resolve())],
                             capture_output=True, text=True, timeout=20)
    assert process.returncode == 0, (process.returncode, process.stdout, process.stderr)
    return json.loads(process.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--enospc-inner', type=Path)
    args = parser.parse_args()
    if args.enospc_inner:
        print(json.dumps(enospc_inner(args.enospc_inner)))
        return
    BASE.mkdir(parents=True, exist_ok=True)
    before = sha(SOURCE)
    # Freeze without importing it; every case launches separate CLI processes.
    shutil.copyfile(SOURCE, FROZEN)
    frozen = sha(FROZEN)
    assert before == frozen
    for name, function in [('repeated', repeated), ('concurrent', concurrent),
                           ('crash-points', crash_points),
                           ('pins-bad-recovery', pins_and_bad_recovery),
                           ('file-size-failure', file_size_failure),
                           ('metadata-pressure', metadata_pressure),
                           ('changed-cache', changed_cache),
                           ('damaged-metadata', damaged_metadata),
                           ('unsafe-links', unsafe_links), ('enospc', enospc)]:
        case(name, function)
    after = sha(SOURCE)
    report = {'source_before': before, 'frozen': frozen, 'source_after': after,
              'frozen_after': sha(FROZEN), 'results': RESULTS}
    (BASE / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'source_before': before, 'source_after': after,
                      'source_changed_during_run': before != after}), flush=True)
    if any(r['result'] == 'FAIL' for r in RESULTS):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
