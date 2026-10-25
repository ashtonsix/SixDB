"""Read the existing Btrfs subvolume quota; never create or change a limit."""

import errno
import fcntl
import os
from pathlib import Path
import re
import struct
import uuid


def existing_parent(path):
    path = Path(path).resolve()
    while not path.exists():
        path = path.parent
    return path


def identity(path):
    """The filesystem UUID and containing subvolume of an existing path."""
    path = existing_parent(path)
    data = bytearray(1024)  # struct btrfs_ioctl_fs_info_args
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    try:
        fcntl.ioctl(descriptor, 0x8400941f, data, True)  # BTRFS_IOC_FS_INFO
        lookup = bytearray(4096)  # struct btrfs_ioctl_ino_lookup_args
        struct.pack_into('=QQ', lookup, 0, 0, 256)  # Containing tree, first inode.
        fcntl.ioctl(descriptor, 0xd0009412, lookup, True)  # BTRFS_IOC_INO_LOOKUP
    finally:
        os.close(descriptor)
    filesystem = str(uuid.UUID(bytes=bytes(data[16:32])))
    rootid = struct.unpack_from('=Q', lookup)[0]
    if not rootid:
        raise ValueError(f'cannot identify the Btrfs subvolume: {path}')
    return filesystem, '0/' + str(rootid)


def covers(settings, path):
    try:
        return identity(path) == (settings['filesystem_uuid'], settings['qgroup'])
    except OSError as error:
        if error.errno in {errno.ENOTTY, errno.ENODEV, errno.EINVAL, errno.EOPNOTSUPP}:
            return False
        raise


def status(settings):
    """Validate the live kernel boundary and report conservative current room.

    A level-zero quota covers this subvolume. Newly created child subvolumes
    have separate quota groups and are deliberately not inferred to be covered.
    The kernel enforces concurrent writes; these counters are a space estimate,
    not an admission reservation or a pathname allocation scan.
    """
    for path in (settings['scope'], settings['root']):
        if not covers(settings, path):
            raise ValueError(f'storage path is outside the configured Btrfs quota: {path}')
    quota = Path('/sys/fs/btrfs') / settings['filesystem_uuid'] / 'qgroups'

    def number(path):
        value = path.read_text().strip()
        if not value.isdigit():
            raise ValueError(f'invalid kernel quota counter: {path}')
        return int(value)

    mode = (quota / 'mode').read_text().strip()
    if (number(quota / 'enabled') != 1 or mode not in {'qgroup', 'squota'} or
            number(quota / 'inconsistent') != 0):
        raise ValueError('configured Btrfs quota is disabled, inconsistent or unsupported')
    group = quota / settings['qgroup'].replace('/', '_')
    limit = number(group / 'max_referenced')
    if number(group / 'limit_flags') != 1 or not 0 < limit <= settings['budget_bytes']:
        raise ValueError('configured Btrfs hard limit was removed or exceeds its saved budget')
    used = number(group / 'referenced')
    reserved = sum(number(group / name) for name in
                   ('rsv_data', 'rsv_meta_pertrans', 'rsv_meta_prealloc'))
    filesystem = os.statvfs(existing_parent(settings['root']))
    free = filesystem.f_bavail * filesystem.f_frsize
    return {'root': str(settings['root']), 'scope': str(settings['scope']),
            'filesystem_uuid': settings['filesystem_uuid'], 'qgroup': settings['qgroup'],
            'quota_mode': mode, 'capacity_bytes': limit, 'used_bytes': used,
            'reserved_bytes': reserved, 'available_bytes': min(free, max(0, limit - used - reserved)),
            'filesystem_available_bytes': free,
            'filesystem_bytes': filesystem.f_blocks * filesystem.f_frsize}


def mountpoint(path):
    """The actual VFS mount containing a canonical path, including bind mounts."""
    unescape = lambda value: re.sub(r'\\([0-7]{3})', lambda m: chr(int(m[1], 8)), value)
    mounts = []
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        fields = line.split(' - ', 1)[0].split()
        mount = Path(unescape(fields[4]))
        if Path(path).is_relative_to(mount):
            mounts.append(mount)
    if not mounts:
        raise ValueError(f'cannot identify the storage mount: {path}')
    return max(mounts, key=lambda mount: len(mount.parts))
