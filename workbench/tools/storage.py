"""Verify native bounded storage and durably publish recoverable research output."""

from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import uuid

GIB = 1024 ** 3


def configuration_path():
    config_home = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    return Path(os.environ.get('SIXDB_STORAGE_CONFIG', config_home / 'sixdb/storage.json')).expanduser()


def canonical_path(path):
    """Prefer native home paths over OrbStack's equivalent home bind mounts."""
    path = Path(path).expanduser().absolute().resolve()
    existing = path
    while not existing.exists():
        existing = existing.parent
    home = Path.home().resolve()
    for parent in (existing, *existing.parents):
        if parent.samefile(home):
            return home / path.relative_to(parent)
    return path


def _configuration():
    path = configuration_path()
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        if 'SIXDB_STORAGE_CONFIG' in os.environ:
            raise ValueError(f'explicit storage configuration is missing: {path}')
        return None
    with os.fdopen(fd, 'rb') as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > 16384:
            raise ValueError('storage configuration must be a bounded regular JSON file')
        data = source.read(16385)
        if len(data) > 16384:
            raise ValueError('storage configuration grew beyond its limit')
    value = json.loads(data)
    fields = {'format', 'root', 'scope', 'filesystem_uuid', 'qgroup', 'budget_bytes'}
    if (not isinstance(value, dict) or set(value) != fields or
            type(value['format']) is not int or value['format'] != 1):
        raise ValueError('invalid native storage configuration')
    for name in ('root', 'scope'):
        raw = value[name]
        if not isinstance(raw, str) or not Path(raw).is_absolute() or '..' in Path(raw).parts:
            raise ValueError(f'invalid storage {name}')
        value[name] = canonical_path(raw)
        if value[name].exists() and not value[name].is_dir():
            raise ValueError(f'storage {name} must be a directory')
    if not value['scope'].is_dir():
        raise ValueError('storage scope must already exist')
    if (not isinstance(value['filesystem_uuid'], str) or
            str(uuid.UUID(value['filesystem_uuid'])) != value['filesystem_uuid'] or
            not isinstance(value['qgroup'], str) or
            not re.fullmatch(r'0/[1-9][0-9]*', value['qgroup']) or
            type(value['budget_bytes']) is not int or value['budget_bytes'] < 4096 or
            value['budget_bytes'] % 4096):
        raise ValueError('invalid native quota identity or budget')
    return value


def _configured():
    settings = _configuration()
    if settings is None:
        return None, None
    import nativequota
    return settings, nativequota.status(settings)


def configuration():
    """A host configuration whose live kernel quota has just been verified."""
    return _configured()[0]


def managed_root():
    settings, _ = _configured()
    return settings['root'] if settings is not None else None


def _check_path(path):
    settings, _ = _configured()
    if settings is not None:
        import nativequota
        if not nativequota.covers(settings, canonical_path(path)):
            raise ValueError(f'output is outside the configured Btrfs quota: {path}')


def check_build(path):
    """Check raw CMake output without creating or relocating anything."""
    settings, _ = _configured()
    if settings is not None:
        import nativequota
        if not nativequota.covers(settings, canonical_path(path)):
            raise ValueError(f'build output is outside the configured Btrfs quota: {path}\n'
                             'Initialize a fresh external worktree before configuring: '
                             f'python3 {Path(__file__).resolve()} ROOT')


def managed_mount(path):
    settings, _ = _configured()
    if settings is None:
        return None
    import nativequota
    path = canonical_path(path)
    if not nativequota.covers(settings, path):
        return None
    return nativequota.mountpoint(path)


def is_managed(path):
    return managed_mount(path) is not None


def immutable_settings():
    settings, _ = _configured()
    if settings is None:
        return None
    # Keep the catalog budget stable as usage changes. The native hard quota
    # separately bounds every byte, including concurrent mutable output.
    root = settings['root'] / 'immutable'
    for path in (root, root / 'objects', root / 'staging'):
        _check_path(path)
    return root, min(10 * GIB, settings['budget_bytes'])


def immutable_store():
    from sixdb_storage.store import Store
    settings = immutable_settings()
    if settings is None:
        raise ValueError('no bounded storage configuration is installed')
    return Store(*settings)


def managed_status():
    return _configured()[1]


def manage_git_worktrees(path):
    """Verify common Git metadata in place; relative commondir must stay valid."""
    settings, _ = _configured()
    if settings is not None:
        import nativequota
        for metadata in (Path(path).parent, path):
            if not nativequota.covers(settings, canonical_path(metadata)):
                raise ValueError('common Git metadata must remain inside the configured Btrfs quota: '
                                 f'{metadata}; use a worktree of a native checkout')
    return canonical_path(path)


def _directory_link(path, destination):
    """Link an explicit empty output location; preserve every nonempty tree."""
    path, destination = Path(path).absolute(), canonical_path(destination)
    _check_path(destination)
    if path.is_symlink():
        if canonical_path(path) != destination:
            raise ValueError(f'storage path already points elsewhere; preserved: {path}')
        return
    if path.exists():
        if not path.is_dir():
            raise ValueError(f'storage path is not a directory; preserved: {path}')
        try:
            path.rmdir()
        except OSError as exc:
            raise ValueError(f'existing output needs verified migration; preserved: {path}') from exc
    destination.mkdir(parents=True, exist_ok=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    os.symlink(destination, path, target_is_directory=True)
    sync_directory(path.parent)


def initialize(root):
    """Verify ordinary build/Git paths and select temporary storage in the hub.

    The native quota already covers writes without this helper, including plain
    CMake and new ordinary Git worktrees. No mount or activation service exists.
    External nonempty builds need explicit verified migration before use.
    """
    managed = managed_root()
    if managed is None:
        return None
    root = canonical_path(root)
    if (root / '.git').exists():
        result = subprocess.run(['git', '-C', str(root), 'rev-parse', '--path-format=absolute',
                                 '--git-common-dir'], capture_output=True, text=True)
        if result.returncode:
            raise ValueError('cannot locate the checkout common Git metadata')
        manage_git_worktrees(Path(result.stdout.strip()) / 'worktrees')
    build = manage_directory(root / 'build')
    build.mkdir(parents=True, exist_ok=True)
    temporary = managed / 'tmp'
    _check_path(temporary)
    temporary.mkdir(parents=True, exist_ok=True)
    os.environ.update(TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    tempfile.tempdir = str(temporary)
    return build


def manage_directory(path, *, root=None):
    """Verify an output location or link an empty external one into the hub."""
    if root is not None:
        initialize(root)
    managed = managed_root()
    path = canonical_path(path)
    if managed is None or is_managed(path):
        return path
    # This entry point is for output directories, never a source checkout or
    # filesystem root. Refuse obvious source roots even when they are empty.
    if path == Path('/') or path == Path.home().resolve() or (path / '.git').exists():
        raise ValueError(f'refusing to replace a source/root directory: {path}')
    key = hashlib.sha256(str(path).encode()).hexdigest()[:24]
    destination = managed / 'locations' / key
    _directory_link(path, destination)
    return destination


def existing_parent(path):
    path = canonical_path(path)
    while not path.exists():
        path = path.parent
    return path


def headroom(path):
    settings, quota = _configured()
    path = existing_parent(path)
    if settings is not None:
        import nativequota
        if not nativequota.covers(settings, path):
            raise ValueError(f'output is outside the configured Btrfs quota: {path}')
    paths = {'destination': path}
    # The guest quota and Mac backing volume remain separate physical limits.
    host = os.environ.get('SIXDB_HOST_STORAGE_PATH')
    if host:
        paths['host'] = existing_parent(host)
    elif Path('/mnt/mac').is_mount():
        paths['Mac host'] = Path('/mnt/mac')
    elif sys.platform == 'darwin':
        paths['Mac host'] = Path.home()
    result = {name: {'path': str(p), 'free': shutil.disk_usage(p).free}
              for name, p in paths.items()}
    if quota is not None:
        result['destination']['free'] = min(result['destination']['free'], quota['available_bytes'])
    return result


def require_space(path, additional=0):
    reserve = int(float(os.environ.get('SIXDB_MIN_FREE_GIB', '5')) * GIB)
    if reserve < 0 or additional < 0:
        raise ValueError('storage reserve and requested space must be nonnegative')
    for name, info in headroom(path).items():
        if info['free'] < additional + reserve:
            raise OSError(28, f"{name} has {info['free'] / GIB:.2f} GiB free; "
                          f"need {additional / GIB:.2f} GiB plus {reserve / GIB:g} GiB reserve. "
                          'Free space and retry; completed worker bundles remain in S3. '
                          'Inspect with worker.py cache; SIXDB_MIN_FREE_GIB adjusts the reserve.')


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
    _check_path(path)
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
    _check_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def transfer_lock():
    managed = managed_root()
    if managed is not None:
        return lock(managed / '.locks/transfer.lock')
    cache = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache'))
    return lock(cache / 'sixdb/transfer.lock')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Verify configured native bounded storage')
    parser.add_argument('root', nargs='?', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--status', action='store_true', help='fresh kernel quota and physical headroom')
    parser.add_argument('--check-build', type=Path,
                        help='check CMake output before configuring; no-op without host configuration')
    args = parser.parse_args()
    try:
        if args.check_build is not None:
            check_build(args.check_build)
            raise SystemExit(0)
        result = managed_status() if args.status else initialize(args.root)
        if result is None:
            raise ValueError('no host storage configuration is installed')
        print(json.dumps(result, sort_keys=True) if args.status else result)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
