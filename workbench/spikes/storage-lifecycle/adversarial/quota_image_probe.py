#!/usr/bin/env python3
"""Bounded disposable ext4-image feasibility probe; never formats a device.

Creates one new 64 MiB regular image, mounts it only in a private root mount
namespace, exercises ordinary writes through real ENOSPC, and removes the image
after saving its report and confirming teardown. No storage manager is involved.
"""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import time
import uuid

SIZE = 64 * 1024 ** 2


def run(argv, **kwargs):
    return subprocess.run(argv, text=True, capture_output=True, **kwargs)


def filesystem(path):
    s = os.statvfs(path)
    return {'bytes': s.f_blocks * s.f_frsize, 'available_bytes': s.f_bavail * s.f_frsize,
            'free_bytes': s.f_bfree * s.f_frsize, 'inodes': s.f_files, 'free_inodes': s.f_ffree}


def checksum(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sync_dir(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def inside(directory):
    image, mountpoint = directory / 'workspace.img', directory / 'mount'
    identity = json.loads((directory / 'created-for-probe.json').read_text())
    info = image.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size != SIZE or
            [info.st_dev, info.st_ino] != identity['image_identity']):
        raise RuntimeError('refuse to format anything except the newly created regular fixture image')
    result = {'image_bytes': SIZE, 'mounted': False, 'stage': 'format'}
    mounted = False
    try:
        formatted = run(['mkfs.ext4', '-F', '-q', '-m', '0', '-N', '2048',
                         '-E', 'lazy_itable_init=0,lazy_journal_init=0', str(image)], timeout=30)
        result['format_returncode'] = formatted.returncode
        if formatted.returncode:
            result['error'] = formatted.stderr[-3000:]
            return result
        result['stage'] = 'mount'
        command = ['mount', '-t', 'ext4', '-o', 'loop,nodev,nosuid,noatime', str(image), str(mountpoint)]
        attached = run(command, timeout=30)
        result['mount_returncode'] = attached.returncode
        if attached.returncode:
            result['error'] = attached.stderr[-3000:]
            return result
        mounted = True
        result['mounted'] = True
        result['initial_filesystem'] = filesystem(mountpoint)
        marker = mountpoint / 'acknowledged-incremental-object'
        with marker.open('xb') as out:
            out.write(b'Previously acknowledged incremental state.\n' * 256)
            out.flush()
            os.fsync(out.fileno())
        sync_dir(mountpoint)
        result['marker_sha256'] = checksum(marker)
        result['stage'] = 'native-bulk-write'
        bulk = run(['dd', 'if=/dev/zero', 'of=' + str(mountpoint / 'bulk'), 'bs=65536',
                    'count=2048', 'conv=fsync', 'status=none'], timeout=30,
                   env=os.environ | {'LC_ALL': 'C'})
        result['bulk'] = {'returncode': bulk.returncode, 'stderr': bulk.stderr[-1000:],
                          'filesystem': filesystem(mountpoint),
                          'marker_preserved': checksum(marker) == result['marker_sha256']}
        (mountpoint / 'bulk').unlink()
        sync_dir(mountpoint)
        result['stage'] = 'many-files'
        small = mountpoint / 'small'
        small.mkdir()
        count = 0
        failure = None
        for index in range(10000):
            try:
                with (small / str(index)).open('xb') as out:
                    out.write(b'x')
                count += 1
            except OSError as exc:
                failure = {'errno': exc.errno, 'message': str(exc)}
                break
        result['many_files'] = {'created': count, 'failure': failure,
                                'filesystem': filesystem(mountpoint),
                                'marker_preserved': checksum(marker) == result['marker_sha256']}
        sync_dir(small)
        sync_dir(mountpoint)
        result['stage'] = 'persistent-remount'
        detached = run(['umount', str(mountpoint)], timeout=30)
        if detached.returncode:
            raise RuntimeError(detached.stderr)
        mounted = False
        remounted = run(command, timeout=30)
        if remounted.returncode:
            raise RuntimeError(remounted.stderr)
        mounted = True
        result['remounted_marker_sha256'] = checksum(marker)
        result['remounted_files'] = len(list((mountpoint / 'small').iterdir()))
        result['final_filesystem'] = filesystem(mountpoint)
        result['image_size_unchanged'] = image.stat().st_size == SIZE
        result['passed'] = (bulk.returncode != 0 and 'No space left on device' in bulk.stderr and
                            result['bulk']['marker_preserved'] and failure and failure['errno'] == errno.ENOSPC and
                            result['many_files']['marker_preserved'] and result['remounted_files'] == count and
                            result['remounted_marker_sha256'] == result['marker_sha256'] and
                            result['image_size_unchanged'])
        result['stage'] = 'complete'
    except Exception as exc:
        result['error'] = f'{type(exc).__name__}: {exc}'
    finally:
        if mounted:
            detached = run(['umount', str(mountpoint)], timeout=30)
            result['final_unmount_returncode'] = detached.returncode
        result['private_mount_torn_down'] = not mountpoint.is_mount()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inside', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/reviews/storage-20261024/quota-image-probe'))
    args = parser.parse_args()
    if args.inside:
        print(json.dumps(inside(args.inside.resolve()), sort_keys=True))
        return
    directory = args.output.resolve() / uuid.uuid4().hex
    directory.mkdir(parents=True, mode=0o700)
    (directory / 'mount').mkdir()
    image = directory / 'workspace.img'
    fd = os.open(image, os.O_CREAT | os.O_EXCL | os.O_WRONLY | os.O_NOFOLLOW, 0o600)
    try:
        os.ftruncate(fd, SIZE)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise RuntimeError('fixture image is not a regular file')
    finally:
        os.close(fd)
    (directory / 'created-for-probe.json').write_text(json.dumps({'image_identity': [info.st_dev, info.st_ino]}))
    started = time.monotonic()
    child = run(['sudo', '-n', 'unshare', '--mount', '--propagation', 'private', '--',
                 sys.executable, str(Path(__file__).resolve()), '--inside', str(directory)], timeout=120)
    try:
        result = json.loads(child.stdout)
    except ValueError:
        result = {'passed': False, 'error': child.stderr[-3000:], 'stdout': child.stdout[-1000:]}
    info = image.stat()
    loops = run(['sudo', '-n', 'losetup', '--associated', str(image)])
    result.update({'fixture_directory': str(directory), 'script_sha256': checksum(Path(__file__)),
                   'outer_returncode': child.returncode, 'seconds': time.monotonic() - started,
                   'image_logical_bytes_after': info.st_size, 'image_allocated_bytes_after': info.st_blocks * 512,
                   'remaining_loop_associations': loops.stdout.strip(), 'loop_query_returncode': loops.returncode})
    safe = loops.returncode == 0 and not loops.stdout.strip() and not (directory / 'mount').is_mount()
    result['teardown_verified'] = safe
    report = directory / 'report.json'
    report.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    if safe:
        image.unlink()
        result['large_image_removed'] = True
        report.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(str(report))


if __name__ == '__main__':
    main()
