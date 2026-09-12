"""Local space checks and durable publication for recoverable research output."""

from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

GIB = 1024 ** 3


def existing_parent(path):
    path = Path(path).resolve()
    while not path.exists():
        path = path.parent
    return path


def headroom(path):
    paths = {'destination': existing_parent(path)}
    # OrbStack's guest disk and its Mac backing volume can have different limits.
    host = os.environ.get('SIXDB_HOST_STORAGE_PATH')
    if host:
        paths['host'] = existing_parent(host)
    elif Path('/mnt/mac').is_mount():
        paths['Mac host'] = Path('/mnt/mac')
    elif sys.platform == 'darwin':
        paths['Mac host'] = Path.home()
    return {name: {'path': str(p), 'free': shutil.disk_usage(p).free}
            for name, p in paths.items()}


def require_space(path, additional=0):
    reserve = int(float(os.environ.get('SIXDB_MIN_FREE_GIB', '5')) * GIB)
    if reserve < 0 or additional < 0:
        raise ValueError('storage reserve and requested space must be nonnegative')
    for name, info in headroom(path).items():
        if info['free'] < additional + reserve:
            raise OSError(28, f"{name} has {info['free'] / GIB:.2f} GiB free; "
                          f"need {additional / GIB:.2f} GiB plus {reserve / GIB:g} GiB reserve. "
                          "Free space and retry; completed worker bundles remain in S3. "
                          "Inspect with worker.py cache; SIXDB_MIN_FREE_GIB adjusts the reserve.")


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def sync_tree(path):
    directories = []
    for directory, _, names in os.walk(path):
        directories.append(Path(directory))
        for name in names:
            with (Path(directory) / name).open('rb') as handle:
                os.fsync(handle.fileno())
    for directory in reversed(directories):
        sync_directory(directory)


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'w') as handle:
            json.dump(value, handle, indent=2, sort_keys=True)
            handle.write('\n')
            handle.flush()
            os.fsync(handle.fileno())
        temporary.replace(path)
        sync_directory(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


@contextmanager
def lock(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def transfer_lock():
    # Shared by controller checkouts so simultaneous downloads don't each
    # spend the same free space. This does not reserve space from other apps.
    cache = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache'))
    return lock(cache / 'sixdb/transfer.lock')
