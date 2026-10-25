#!/usr/bin/env python3
"""Independent tiny composed-flow capacity/process review; no production paths.

Run on Linux with the already frozen candidate under the exact private root.
Four accepted 16 MiB images maximum; input and observation fixtures < 8 MiB.
The driver may be invoked as an unprivileged consumer by the candidate.
"""
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import time

ROOT = Path('/home/ashtonsix/sixdb/build/reviews/storage-20261024/fresh-composed-capacity-v5')
SIZE = 16 * 1024**2
BUDGET = SIZE + 1024**2
HERE = Path(__file__).resolve()
PAYLOAD = b'independent accepted input v5\n' * 100
SENTINEL = b'accepted unique output must survive\n' * 50
EXPECTED = {
    'store.py': 'b0a4c22be48aba7a19bd9bd61d86e30b90392bcc7999cd7a824175753122b771',
    'acquire.py': '7ff36f2abf2ef6e84d9fdaf544af89793aa6851ba51852b8519dc49927d32970',
    'workspace.py': 'fb2cc7522d873bdb90273e639c15212a65ecc2839870c59aeb4d7cc3252b542f',
    'flow.py': '122e1b75a5ece57c396d993a9e554108f0b704c0d1c660cd6d5a88af6004cfd6',
}


def emit(value):
    print(json.dumps(value, sort_keys=True), flush=True)


def command(argv, check=True):
    result = subprocess.run(argv, capture_output=True, timeout=30)
    if check and result.returncode:
        raise RuntimeError((argv, result.returncode, result.stderr.decode(errors='replace')[-1500:]))
    return result


def root_command(argv, check=True):
    return command(['sudo', '-n', *argv], check)


def image(pool, name='one'):
    return ROOT / pool / (name + '.img')


def debug(pool, request, name='one'):
    return root_command(['/usr/sbin/debugfs', '-R', request, str(image(pool, name))]).stdout


def no_associations(pool):
    # Check all manager files independently of the candidate's report.
    paths = root_command(['/usr/bin/find', str(ROOT / pool), '-maxdepth', '1', '-type', 'f']).stdout.decode().splitlines()
    observations = []
    for filename in paths:
        if filename.endswith(('.img', '.new')):
            association = root_command(['/usr/sbin/losetup', '--associated', filename]).stdout.decode().strip()
            assert not association, association
            observations.append(filename)
    # A private namespace can retain a mount invisible to our mount namespace;
    # any ext4 mount of one of our image loops would retain its loop association.
    return observations


def image_oracle(pool, name='one', sentinel=True):
    no_associations(pool)
    data = root_command(['python3', '-c',
        'import os,json,sys; s=os.stat(sys.argv[1]); print(json.dumps({"size":s.st_size,"blocks":s.st_blocks*512,"links":s.st_nlink,"uid":s.st_uid}))',
        str(image(pool, name))]).stdout
    result = json.loads(data)
    assert result['size'] == SIZE and result['blocks'] >= SIZE and result['links'] == 1, result
    result['superblock'] = debug(pool, 'stats', name).decode()
    if sentinel:
        actual = debug(pool, 'cat /work/output/shared/sentinel', name)
        assert actual == SENTINEL, (pool, name, actual[:200])
        result['sentinel_sha256'] = hashlib.sha256(actual).hexdigest()
    return result


def flow(pool, mode, token='', name='one', extra=()):
    return [sys.executable, str(ROOT / 'candidate/flow.py'), '--pool', str(ROOT / pool),
            '--budget', str(BUDGET), '--size', str(SIZE), '--cache-budget', str(1024**2),
            'run', name, str(ROOT / 'input.json'), str(ROOT / 'input.tar.gz'), '--output', 'shared',
            '--', sys.executable, str(HERE), 'consumer', mode, token, *extra]


def run(pool, mode, token='', name='one', extra=(), wanted=None):
    before = time.monotonic()
    completed = command(flow(pool, mode, token, name, extra), check=False)
    record = {'mode': mode, 'pool': pool, 'name': name, 'returncode': completed.returncode,
              'elapsed_seconds': time.monotonic() - before,
              'stdout': completed.stdout.decode(errors='replace'),
              'stderr': completed.stderr.decode(errors='replace')}
    emit(record)
    RESULTS.append(record)
    if wanted is not None:
        assert completed.returncode == wanted, record
    return record


def proc_identity(pid):
    try:
        stat = Path(f'/proc/{pid}/stat').read_text()
        fields = stat[stat.rindex(')') + 2:].split()
        args = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
        return {'pid': pid, 'start': fields[19], 'state': fields[0],
                'args': [part.decode(errors='replace') for part in args if part]}
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return None


def children(token):
    found = []
    for item in Path('/proc').iterdir():
        if item.name.isdigit():
            identity = proc_identity(int(item.name))
            if identity and identity['args'][1:3] == [str(HERE), 'child'] and token in identity['args']:
                found.append(identity)
    return found


def wait_children(token, count=2):
    end = time.monotonic() + 10
    while time.monotonic() < end:
        found = children(token)
        if len(found) == count:
            return found
        time.sleep(.02)
    raise AssertionError(('children never ready', token, found))


def wait_absent(identities):
    end = time.monotonic() + 10
    while time.monotonic() < end:
        alive = [old for old in identities if (new := proc_identity(old['pid'])) and new['start'] == old['start']]
        if not alive:
            return
        time.sleep(.02)
    raise AssertionError(('descendants remain', alive))


def consumer(mode, token, *extra):
    mapping = json.load(os.fdopen(int(os.environ['SIXDB_INPUTS_FD'])))
    assert len(mapping) == 1 and Path(next(iter(mapping.values()))).read_bytes() == PAYLOAD
    if mode == 'seed':
        Path('sentinel').write_bytes(SENTINEL)
        with open('sentinel', 'rb') as stream:
            os.fsync(stream.fileno())
        return
    assert Path('sentinel').read_bytes() == SENTINEL
    def observation(value):
        # Overwrite a preallocated block so a full disk still leaves a readable
        # external observation. The review independently reads it with debugfs.
        encoded = json.dumps(value).encode()
        assert len(encoded) < 4096
        os.pwrite(observation_fd, encoded + bytes(4096 - len(encoded)), 0)
        os.fsync(observation_fd)
        os.close(observation_fd)
    if mode in ('bytes', 'unlinked', 'inodes'):
        observation_fd = os.open('pressure-observation', os.O_CREAT | os.O_RDWR, 0o600)
        os.posix_fallocate(observation_fd, 0, 4096)
    if mode == 'verify':
        return
    if mode == 'cleanup':
        for path in Path('.').glob('pressure*'):
            if path.is_dir():
                for entry in path.iterdir():
                    entry.unlink()
                path.rmdir()
            else:
                path.unlink()
        return
    if mode in ('bytes', 'unlinked'):
        fd = os.open('pressure-data', os.O_CREAT | os.O_RDWR, 0o600)
        if mode == 'unlinked':
            os.unlink('pressure-data')
        copied = 0
        try:
            while copied < SIZE * 2:
                copied += os.write(fd, b'X' * 65536)
        except OSError as error:
            assert error.errno == errno.ENOSPC, error
            fs = os.fstatvfs(fd)
            allocated = os.fstat(fd).st_blocks * 512
            # Keep the unlinked inode open until the external reviewer inspects it.
            if mode == 'unlinked':
                time.sleep(float(extra[0]) if extra else 1)
            os.close(fd)
            observation({'pressure': mode, 'errno': error.errno, 'copied': copied,
                  'inode_allocated': allocated, 'available': fs.f_bavail * fs.f_frsize,
                  'free_inodes': fs.f_ffree})
            return
        raise AssertionError('no ENOSPC within twice the image capacity')
    if mode == 'inodes':
        directory = Path('pressure-inodes')
        directory.mkdir()
        count = 0
        try:
            for index in range(10000):
                (directory / str(index)).touch(exist_ok=False)
                count += 1
        except OSError as error:
            assert error.errno == errno.ENOSPC, error
            fs = os.statvfs('.')
            observation({'pressure': mode, 'errno': error.errno, 'count': count,
                  'available': fs.f_bavail * fs.f_frsize, 'free_inodes': fs.f_ffree})
            return
        raise AssertionError('no inode exhaustion within bounded fixture')
    if mode == 'logs':
        copied = 0
        try:
            while copied < SIZE * 2:
                copied += os.write(1, b'L' * 65536)
        except OSError as error:
            assert error.errno == errno.ENOSPC, error
            return
        raise AssertionError('stdout exceeded fixed image capacity without ENOSPC')
    if mode in ('wait', 'exit', 'sleep'):
        if mode == 'sleep':
            time.sleep(2)
            return
        descendants = [subprocess.Popen([sys.executable, str(HERE), 'child', token, kind],
                                         start_new_session=True) for kind in ('hold', 'fork')]
        if mode == 'exit':
            # Parent must not exit before both children have created their held fds.
            for child in descendants:
                assert child.poll() is None
            time.sleep(.4)
            return
        while True:
            time.sleep(.1)
    raise AssertionError(mode)


def child(token, kind):
    if kind == 'fork':
        # Detach once more; the namespace's PID 1 must still reap this descendant.
        if os.fork():
            return
        os.setsid()
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    fd = os.open('pressure-descendant-' + kind, os.O_CREAT | os.O_RDWR, 0o600)
    os.unlink('pressure-descendant-' + kind)
    os.write(fd, b'D' * 65536)
    while True:
        time.sleep(.1)


def lifecycle(pool, target):
    token = 'composed-capacity-v5-' + target
    process = subprocess.Popen(flow(pool, 'wait', token), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    identities = wait_children(token)
    if target == 'wrapper':
        os.kill(process.pid, signal.SIGKILL)
    elif target == 'consumer':
        # The exact consumer is identified outside the private PID namespace.
        matches = []
        for entry in Path('/proc').iterdir():
            if entry.name.isdigit():
                identity = proc_identity(int(entry.name))
                if identity and identity['args'][1:4] == [str(HERE), 'consumer', 'wait'] and token in identity['args']:
                    matches.append(identity)
        assert len(matches) == 1, matches
        os.kill(matches[0]['pid'], signal.SIGKILL)
    else:
        raise AssertionError(target)
    stdout, stderr = process.communicate(timeout=15)
    wait_absent(identities)
    no_associations(pool)
    result = {'target': target, 'wrapper_returncode': process.returncode,
              'descendants': identities, 'stdout': stdout.decode(errors='replace'),
              'stderr': stderr.decode(errors='replace'), 'remaining': children(token)}
    RESULTS.append(result)
    emit(result)
    run(pool, 'verify', wanted=0)
    ORACLES[target] = image_oracle(pool)


def recover_inode_fixture():
    recovery = [sys.executable, str(ROOT / 'candidate/workspace.py'), '--pool', str(ROOT / 'pressure'),
                '--budget', str(BUDGET), 'run', 'one', '--size', str(SIZE), '--', sys.executable,
                '-c', 'import pathlib; p=pathlib.Path("output/shared/pressure-inodes"); '
                      '[f.unlink() for f in p.iterdir()]; p.rmdir(); '
                      'pathlib.Path("output/shared/pressure-observation").unlink()']
    result = command(recovery, check=False)
    record = {'fixture_recovery_via_workspace': result.returncode,
              'stdout': result.stdout.decode(errors='replace'), 'stderr': result.stderr.decode(errors='replace')}
    RESULTS.append(record)
    emit(record)
    assert result.returncode == 0, record
    no_associations('pressure')


def finish_existing():
    """Continue after a capacity failure, without allocating another image."""
    previous = json.loads((ROOT / 'results.json').read_text())
    RESULTS.extend(previous['runs'])
    ORACLES.update(previous['oracles'])
    ORACLES['concurrent-images'] = {name: image_oracle('concurrent', name) for name in ('alpha', 'beta')}
    token = 'composed-capacity-v5-live-unlinked'
    process = subprocess.Popen(flow('pressure', 'unlinked', token, extra=('3',)),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    live = []
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        for entry in Path('/proc').iterdir():
            if entry.name.isdigit():
                identity = proc_identity(int(entry.name))
                if identity and identity['args'][1:4] == [str(HERE), 'consumer', 'unlinked'] and token in identity['args']:
                    inspection = root_command(['python3', '-c',
                        'import os,json,sys,pathlib; p=pathlib.Path("/proc")/sys.argv[1]/"fd"; a=[]; '
                        '\nfor f in p.iterdir():\n'
                        ' try:\n'
                        '  dest=os.readlink(f); s=os.stat(f)\n'
                        '  if dest.endswith(" (deleted)"): a.append(dict(fd=f.name,path=dest,bytes=s.st_size,allocated=s.st_blocks*512,links=s.st_nlink))\n'
                        ' except FileNotFoundError: pass\n'
                        'print(json.dumps(a))', str(identity['pid'])], check=False)
                    if inspection.returncode == 0:
                        live = json.loads(inspection.stdout)
        if any(row['allocated'] > 8 * 1024**2 and row['links'] == 0 for row in live):
            break
        time.sleep(.02)
    stdout, stderr = process.communicate(timeout=15)
    assert process.returncode == 0 and live, (process.returncode, live, stderr)
    ORACLES['live-unlinked'] = {'fds': live, 'stdout': stdout.decode(errors='replace')}
    emit(ORACLES['live-unlinked'])
    no_associations('pressure')
    run('pressure', 'verify', wanted=0)
    # Bounded interrupted-before-allocation staging, zero data allocation.
    root_command(['python3', '-c',
        'import os,sys; f=os.open(sys.argv[1],os.O_CREAT|os.O_EXCL|os.O_RDWR,0o600); os.fsync(f); os.close(f)',
        str(ROOT / 'pressure/stale.new')])
    run('pressure', 'verify', wanted=0)
    assert root_command(['/usr/bin/test', '-e', str(ROOT / 'pressure/stale.new')], check=False).returncode != 0
    ORACLES['empty-staging-reconcile'] = image_oracle('pressure')
    run('pressure', 'logs', wanted=0)
    ORACLES['logs'] = image_oracle('pressure')
    ORACLES['logs']['stdout_inode'] = debug('pressure', 'stat /stdout').decode()
    run('pressure', 'verify')
    no_associations('pressure')
    return 0


def main(resume=False):
    assert os.geteuid() != 0
    for name, wanted in EXPECTED.items():
        assert hashlib.sha256((ROOT / 'candidate' / name).read_bytes()).hexdigest() == wanted
    if not resume:
        assert not (ROOT / 'input.tar.gz').exists(), 'fresh harness run required'
        with tarfile.open(ROOT / 'input.tar.gz', 'w:gz', format=tarfile.USTAR_FORMAT) as archive:
            entry = tarfile.TarInfo('input.bin')
            entry.size = len(PAYLOAD)
            archive.addfile(entry, io.BytesIO(PAYLOAD))
        compressed = (ROOT / 'input.tar.gz').read_bytes()
        (ROOT / 'input.json').write_text(json.dumps({'format': 1, 'bytes': len(compressed),
            'files': 1, 'sha256': hashlib.sha256(compressed).hexdigest()}) + '\n')
        run('pressure', 'seed', wanted=0)
        ORACLES['initial'] = image_oracle('pressure')
    else:
        previous = json.loads((ROOT / 'results.json').read_text())
        RESULTS.extend(previous['runs'])
        ORACLES.update(previous['oracles'])
        recover_inode_fixture()
    for mode in (('unlinked',) if resume else ('bytes', 'inodes', 'unlinked')):
        run('pressure', mode, wanted=0)
        ORACLES[mode] = image_oracle('pressure')
        ORACLES[mode]['pressure_observation'] = json.loads(debug('pressure',
            'cat /work/output/shared/pressure-observation').rstrip(b'\0'))
        if mode == 'bytes':
            ORACLES[mode]['data_inode'] = debug('pressure',
                'stat /work/output/shared/pressure-data').decode()
        assert ORACLES[mode]['pressure_observation']['errno'] == errno.ENOSPC
        if mode != 'unlinked':
            cleanup = run('pressure', 'cleanup')
            if mode == 'inodes' and cleanup['returncode'] == 125:
                RESULTS.append({'failure': 'composed inode cleanup cannot reach consumer',
                                'direct_image_free_inodes': 0})
                recover_inode_fixture()
            else:
                assert cleanup['returncode'] == 0, cleanup
        run('pressure', 'verify', wanted=0)

    run('lifetime', 'seed', wanted=0)
    for target in ('wrapper', 'consumer'):
        lifecycle('lifetime', target)
    run('lifetime', 'exit', 'composed-capacity-v5-exit', wanted=0)
    assert not children('composed-capacity-v5-exit')
    ORACLES['exit'] = image_oracle('lifetime')

    # Different names race for the same one-image budget. Only the accepted one
    # may consume a 16 MiB backing allocation; both image allocations cannot fit.
    processes = [(name, subprocess.Popen(flow('concurrent', 'seed', name=name),
                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)) for name in ('alpha', 'beta')]
    race = []
    for name, process in processes:
        stdout, stderr = process.communicate(timeout=30)
        race.append({'name': name, 'returncode': process.returncode,
                     'stdout': stdout.decode(errors='replace'), 'stderr': stderr.decode(errors='replace')})
    assert sorted(item['returncode'] for item in race) == [0, 125], race
    accepted = next(item['name'] for item in race if item['returncode'] == 0)
    names = root_command(['/usr/bin/find', str(ROOT / 'concurrent'), '-maxdepth', '1', '-name', '*.img']).stdout.decode().splitlines()
    assert len(names) == 1, names
    ORACLES['concurrent'] = image_oracle('concurrent', accepted)
    RESULTS.append({'race': race, 'accepted_images': names})
    emit(RESULTS[-1])

    # An allocation interrupted before acknowledgement is represented by .new.
    # Its root-owned, regular, bounded inode is independently constructed; the
    # accepted workspace is not used as disposable reconciliation material.
    root_command(['/usr/bin/mkdir', '-m', '711', str(ROOT / 'reconcile')])
    root_command(['python3', '-c',
        'import os,sys; f=os.open(sys.argv[1],os.O_CREAT|os.O_EXCL|os.O_RDWR,0o600); os.posix_fallocate(f,0,1048576); os.fsync(f); os.close(f)',
        str(ROOT / 'reconcile/stale.new')])
    run('reconcile', 'seed', wanted=0)
    assert not root_command(['/usr/bin/test', '-e', str(ROOT / 'reconcile/stale.new')], check=False).returncode == 0
    ORACLES['reconcile'] = image_oracle('reconcile')

    # Root-owned appended stdout is deliberately exhausted last: its retained
    # bytes may prevent a later command from opening its cache transaction.
    run('pressure', 'logs', wanted=0)
    ORACLES['logs'] = image_oracle('pressure')
    run('pressure', 'verify')
    no_associations('pressure')
    return 0


RESULTS = []
ORACLES = {}
if __name__ == '__main__':
    if sys.argv[1:2] == ['consumer']:
        consumer(*sys.argv[2:])
    elif sys.argv[1:2] == ['child']:
        child(*sys.argv[2:])
    else:
        try:
            code = finish_existing() if sys.argv[1:] == ['finish-existing'] else main(resume=sys.argv[1:] == ['resume'])
        finally:
            (ROOT / 'results.json').write_text(json.dumps({'runs': RESULTS, 'oracles': ORACLES}, indent=2) + '\n')
        sys.exit(code)
