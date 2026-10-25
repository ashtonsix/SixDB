#!/usr/bin/env python3
"""Persistent, visible fixed-capacity storage; no per-command sandbox or daemon.

Call ensure(pool, budget, size) from an ordinary Linux process. The returned
Path is shared by ordinary tools and IDE readers in the caller's mount namespace.
Only explicitly isolated fixture namespaces have been used during this spike.
"""
from __future__ import annotations

import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys

sys.dont_write_bytecode = True

try:
    from . import workspace as images
except ImportError:
    import workspace as images

MAX_BYTES = (1 << 63) - 1
DEFAULT_BACKING_RESERVE = 64 * 1024**2
IMAGE = 'shared.img'


def validate(budget, size, reserve):
    if (type(size) is not int or type(budget) is not int or type(reserve) is not int or
            size < images.MIN_IMAGE or size % 4096 or budget > MAX_BYTES or
            size + images.MANAGER_RESERVE > budget or not 0 <= reserve <= MAX_BYTES):
        raise ValueError('finite block-aligned size >=16 MiB and budget >=size+1 MiB are required')


def mount_record(target):
    def unescape(value):
        return re.sub(r'\\([0-7]{3})', lambda m: chr(int(m.group(1), 8)), value)
    found = []
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        before, after = line.split(' - ', 1)
        left, right = before.split(), after.split()
        if unescape(left[4]) == str(target):
            found.append({'id': int(left[0]), 'device': left[2], 'root': unescape(left[3]),
                          'options': set(left[5].split(',')), 'type': right[0],
                          'source': unescape(right[1]), 'super_options': set(right[2].split(','))})
    if len(found) > 1:
        raise RuntimeError('stacked mounts at the managed path; preserve and investigate')
    return found[0] if found else None


def validate_mount(record, imagefd):
    if (record['type'] != 'ext4' or record['root'] != '/' or
            not {'rw', 'nodev', 'nosuid'} <= record['options'] or
            not re.fullmatch(r'/dev/loop[0-9]+', record['source'])):
        raise RuntimeError('managed path has an unexpected filesystem/source/options')
    fd = os.open(record['source'], os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        info = os.fstat(fd)
        if not stat.S_ISBLK(info.st_mode) or f'{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}' != record['device']:
            raise RuntimeError('mount source device identity differs')
        status = bytearray(232)  # struct loop_info64 from linux/loop.h
        fcntl.ioctl(fd, 0x4c05, status, True)  # LOOP_GET_STATUS64
        device, inode, _, offset, limit = struct.unpack_from('=QQQQQ', status)
        flags = struct.unpack_from('=I', status, 52)[0]
        image = images.regular(imagefd)
        if ((device, inode) != (image.st_dev, image.st_ino) or offset != 0 or
                limit not in (0, image.st_size) or flags & 1 or not flags & 4):
            raise RuntimeError('loop backing inode/offset/capacity/flags differs')
        if os.fstat(imagefd).st_size != image.st_size:
            raise RuntimeError('image changed during validation')
    finally:
        os.close(fd)


def manager_allocation(pool, underlying_mount):
    # Visible manager allocation only; Btrfs tree/snapshot and host VM overhead
    # cannot be reconstructed from these inodes and are not claimed here.
    total = os.fstat(pool).st_blocks * 512 + os.fstat(underlying_mount).st_blocks * 512
    for name in ('lock', 'budget'):
        fd = images.open_at(pool, name, os.O_RDONLY)
        try:
            total += images.regular(fd).st_blocks * 512
        finally:
            os.close(fd)
    return total


def root_describe(pool_path, budget, size, reserve):
    validate(budget, size, reserve)
    if os.geteuid() != 0:
        raise RuntimeError('root mount continuation must run through sudo')
    uid, gid = int(os.environ.get('SUDO_UID', '0')), int(os.environ.get('SUDO_GID', '0'))
    if uid <= 0 or gid < 0:
        raise RuntimeError('invoke as an ordinary user through sudo -n')
    pool_path = Path(pool_path)
    pool = images.directory(pool_path, create=True)
    lock = images.open_at(pool, 'lock', os.O_CREAT | os.O_RDWR)
    info = images.regular(lock)
    if info.st_size or stat.S_IMODE(info.st_mode) != 0o600:
        raise RuntimeError('unexpected pool lock content/mode')
    imagefd = mountfd = None
    mounted_here = False
    target = pool_path / 'mount'
    try:
        fcntl.flock(lock, fcntl.LOCK_EX)
        entries = set(images.fresh_listdir(pool))
        if entries - {'lock', 'budget', 'mount', IMAGE, 'shared.new'}:
            raise RuntimeError('pool is not a single shared-volume pool; preserve existing entries')
        record = mount_record(target)
        if record is not None:
            if 'shared.new' in entries or IMAGE not in entries or 'budget' not in entries:
                raise RuntimeError('mounted volume has ambiguous/missing backing metadata')
            images.ensure_budget(pool, budget)
            imagefd = images.open_at(pool, IMAGE, os.O_RDWR)
            image = images.regular(imagefd)
            if image.st_size != size or stat.S_IMODE(image.st_mode) != 0o600:
                raise RuntimeError('accepted image capacity/mode is immutable')
            validate_mount(record, imagefd)
            # Do not mutate allocation or create metadata on full-volume reuse.
        else:
            # Physical guest-filesystem admission precedes allocating writes.
            allocated = 0
            if IMAGE in entries:
                fd = images.open_at(pool, IMAGE, os.O_RDONLY)
                try:
                    allocated = images.regular(fd).st_blocks * 512
                finally:
                    os.close(fd)
            available = os.fstatvfs(pool).f_bavail * os.fstatvfs(pool).f_frsize
            needed = max(0, size - allocated) + reserve
            if available < needed:
                raise RuntimeError(f'backing filesystem admission refused: need {needed}, available {available}')
            request = {'name': 'shared', 'size': size, 'budget': budget}
            imagefd, _ = images.prepare(pool, request, maximum_image=size, format_timeout=600)
            mountfd = os.open('mount', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=pool)
            image = images.regular(imagefd)
            if image.st_blocks * 512 + manager_allocation(pool, mountfd) > budget:
                raise RuntimeError('visible backing/manager allocation exceeds budget; preserve image')
            images.tool(['mount', '-t', 'ext4', '-o', 'loop,nodev,nosuid,noatime',
                         images.procpath(imagefd), images.procpath(pool, 'mount')], (pool, imagefd))
            mounted_here = True
            record = mount_record(target)
            if record is None:
                raise RuntimeError('mounted volume is missing from this namespace')
            validate_mount(record, imagefd)
        if images.regular(imagefd).st_blocks * 512 < size:
            raise RuntimeError('backing image allocation is incomplete; preserve and investigate')
        # Only this one stable directory is mandatory. Existing reuse does not
        # create category/tmp/cache files, and remains useful at zero free inodes.
        root = target / 'work'
        try:
            os.mkdir(root, 0o700)
        except FileExistsError:
            work = root.lstat()
            if not stat.S_ISDIR(work.st_mode) or (work.st_uid, work.st_gid) != (uid, gid):
                raise RuntimeError('existing work directory type/owner differs; preserve')
        else:
            os.chown(root, uid, gid, follow_symlinks=False)
            fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
            fd = os.open(target, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        fs = os.statvfs(root)
        image = images.regular(imagefd)
        return {'root': str(root), 'mount': str(target), 'image': str(pool_path / IMAGE),
                'capacity_bytes': size, 'budget_bytes': budget,
                'image_allocated_bytes': image.st_blocks * 512,
                'available_bytes': fs.f_bavail * fs.f_frsize, 'free_inodes': fs.f_ffree,
                'mount_id': record['id'], 'device': record['device'], 'mounted_now': mounted_here,
                'backing_reserve_bytes': reserve, 'owner_uid': uid, 'owner_gid': gid}
    except BaseException:
        if mounted_here:
            images.tool(['umount', images.procpath(pool, 'mount')], (pool,))
        raise
    finally:
        if mountfd is not None:
            os.close(mountfd)
        if imagefd is not None:
            os.close(imagefd)
        os.close(lock)
        os.close(pool)


def describe(pool, budget, size, *, backing_reserve_bytes=DEFAULT_BACKING_RESERVE):
    """Ensure/reuse a volume in this namespace and return bounded diagnostics."""
    validate(budget, size, backing_reserve_bytes)
    argv = ['sudo', '-n', sys.executable, str(Path(__file__).resolve()), '--_root',
            '--pool', str(Path(pool).absolute()), '--budget', str(budget), '--size', str(size),
            '--backing-reserve', str(backing_reserve_bytes)]
    result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'volume helper refused')
    if len(result.stdout) > 16384:
        raise RuntimeError('unexpected volume helper response size')
    return json.loads(result.stdout)


def ensure(pool, budget, size):
    """Return the stable Path shared by builds, workers, datasets and IDE readers."""
    return Path(describe(pool, budget, size)['root'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--_root', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--pool', required=True, type=Path)
    parser.add_argument('--budget', required=True, type=int)
    parser.add_argument('--size', required=True, type=int)
    parser.add_argument('--backing-reserve', type=int, default=DEFAULT_BACKING_RESERVE)
    args = parser.parse_args()
    result = (root_describe(args.pool.absolute(), args.budget, args.size, args.backing_reserve)
              if args._root else describe(args.pool, args.budget, args.size,
                                          backing_reserve_bytes=args.backing_reserve))
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'volume refused: {type(error).__name__}: {error}', file=sys.stderr)
        raise SystemExit(125)
