"""Stream checked archive members into private, atomically published output.

The caller holds storage.transfer_lock. Pending data is disposable cache work;
accepted result directories and differing researcher files are never swept.
"""
from contextlib import closing, contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import stat

import storage
from . import preparation
from .acquire import acquire, inspect_archive, scoped_members

KEY = hashlib.sha256(b'sixdb-archive-transfer-v1').hexdigest()
PUBLIC_KEY = hashlib.sha256(b'sixdb-public-publication-v1').hexdigest()
EXPORT_KEY = hashlib.sha256(b'sixdb-retained-publication-v1').hexdigest()


def publish_directory(source, destination):
    """Atomically install a directory without replacing a concurrently added path."""
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.renameat2(-100, os.fsencode(source), -100, os.fsencode(destination), 1) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(destination))


def staging_parent(destination, *, reclaim_keys=(KEY,)):
    managed = storage.managed_root()
    mount = storage.managed_mount(destination)
    if managed is None or mount is None:
        raise ValueError('archive output is outside the configured storage boundary')
    # All accepted output shares this native subvolume. Known home bind aliases
    # are canonicalized before publication; there are no per-build mounts.
    parent = managed / 'transfers'
    if not storage.is_managed(parent):
        raise ValueError('transfer staging is outside the configured storage boundary')
    if parent.exists():
        for key in reclaim_keys:
            preparation.reclaim(parent, key)
    parent.mkdir(mode=0o700, exist_ok=True)
    if parent.is_symlink():
        raise ValueError('transfer pending namespace was replaced by a link')
    return parent


@contextmanager
def publication(destination):
    """Own the entire public operation, including nested input recovery.

    This lock deliberately differs from the archive transfer lock. A prepared
    dependency can acquire that lock again after its enclosing archive returns.
    One fixed pending root also bounds different-name retries.
    """
    managed = storage.managed_root()
    if managed is None:
        raise ValueError('public publication needs configured storage')
    with storage.lock(managed / '.locks/publication.lock'):
        with storage.transfer_lock():
            parent = staging_parent(destination, reclaim_keys=(PUBLIC_KEY,))
        with preparation.stage(parent, PUBLIC_KEY) as pending:
            yield pending


def copy_checked(source, target, expected, mode):
    """Preallocate and copy exactly a previously checked member size/hash."""
    size = expected['bytes']
    descriptor = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, 'rb') as content:
        info = os.fstat(content.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size != size:
            raise ValueError('copy source type/size changed')
        descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        with os.fdopen(descriptor, 'wb') as output:
            if size:
                os.posix_fallocate(output.fileno(), 0, size)
            digest, remaining = hashlib.sha256(), size
            while remaining:
                block = content.read(min(65536, remaining))
                if not block:
                    raise ValueError('copy source ended before its checked length')
                output.write(block)
                digest.update(block)
                remaining -= len(block)
            if content.read(1) or digest.hexdigest() != expected['sha256']:
                raise ValueError('copy source differs from its checked hash/length')
            output.flush()
            os.fchmod(output.fileno(), mode & 0o777)
            os.fsync(output.fileno())


@contextmanager
def export_stage(destination):
    """One known-size authored-source publication, under publication().

    Cross-filesystem directory rename needs a sibling on the source filesystem.
    Record that one owned location before creating any selected export bodies;
    the next retain reclaims it even when the requested destination changes.
    """
    managed = storage.managed_root()
    index = managed / 'export-publication.json'
    key = hashlib.sha256((EXPORT_KEY + str(managed)).encode()).hexdigest()
    if index.exists() or index.is_symlink():
        if index.is_symlink() or index.stat().st_size > 16384:
            raise ValueError('invalid export publication pointer; preserved')
        previous = json.loads(index.read_text())
        if (not isinstance(previous, dict) or set(previous) != {'format', 'parent', 'key'} or
                previous['format'] != 1 or previous['key'] != key or
                not isinstance(previous['parent'], str)):
            raise ValueError('invalid export publication pointer; preserved')
        parent = Path(previous['parent'])
        if not parent.is_absolute() or parent.resolve() != parent or parent.is_symlink():
            raise ValueError('export publication parent changed; preserved')
        if parent.exists():
            preparation.reclaim(parent, key)
    parent = Path(destination).parent
    parent.mkdir(parents=True, exist_ok=True)
    if parent.resolve() != parent or parent.is_symlink():
        raise ValueError('export publication parent changed; preserved')
    storage.write_json(index, {'format': 1, 'parent': str(parent), 'key': key})
    with preparation.stage(parent, key) as pending:
        yield pending
    # A completed export must not depend on its old destination still existing.
    index.unlink()
    storage.sync_directory(index.parent)


def input_metadata(reference):
    """Read checked dependency metadata without owning a public pending tree."""
    with storage.transfer_lock():
        with closing(storage.immutable_store()) as store:
            inspected = inspect_archive(reference, store=store)
            members = scoped_members(reference, inspected['members'])
            if 'input-artifacts.json' not in members:
                return None
            if 'run.json' not in members:
                raise ValueError('input references have no run receipt')
            result = acquire(store, reference, selected=['run.json', 'input-artifacts.json'], consumer=True,
                             expected={key: inspected[key] for key in ('artifact', 'members')})
            return {name: json.loads(Path(path).read_text()) for name, path in result['paths'].items()}


def restore(reference, destination, *, selected=None, omit_compiler=False,
            omit_source_archive=False, validate=None):
    """Return full scoped member hashes; materialize only requested private files."""
    destination = storage.canonical_path(Path(destination).parent) / Path(destination).name
    if destination.exists() or destination.is_symlink():
        raise ValueError('fetch destination already exists')
    parent = staging_parent(destination.parent)
    with preparation.stage(parent, KEY) as pending:
        with closing(storage.immutable_store()) as store:
            inspected = inspect_archive(reference, store=store, include_modes=True)
            members = scoped_members(reference, inspected['members'])
            wanted = set(members) if selected is None else set(selected)
            if wanted - members.keys():
                raise ValueError('selected files missing from bundle: ' + ', '.join(sorted(wanted - members.keys())))
            if selected is None:
                wanted = {name for name in wanted if not (
                    omit_compiler and members[name]['rebuildable'] or
                    omit_source_archive and Path(name).name in ('source.tar.gz', 'source.tar.zst'))}
            result = acquire(store, reference, selected=sorted(wanted), consumer=True,
                             expected={key: inspected[key] for key in ('artifact', 'members')})
            directories = {parent for name in wanted for parent in Path(name).parents if parent != Path('.')}
            metadata = (len(wanted) + len(directories) + 4) * 4096
            payload = sum((members[name]['bytes'] + 4095) // 4096 * 4096 for name in wanted)
            store.admit(payload + metadata, external=True,
                        inodes=len(wanted) + len(directories) + 4)
            prefix = reference.get('subdirectory', '')
            for name, source in result['paths'].items():
                target = pending / name
                target.parent.mkdir(parents=True, exist_ok=True)
                # Every destination body and partial copy is on the enforcing
                # filesystem. Pending ownership survives controller interruption.
                full_name = prefix + '/' + name if prefix else name
                copy_checked(source, target, members[name], inspected['modes'][full_name])
        if validate is not None:
            validate(pending, members)
        storage.sync_tree(pending)
        destination.parent.mkdir(parents=True, exist_ok=True)
        publish_directory(pending, destination)
        storage.sync_directory(destination.parent)
        return members
