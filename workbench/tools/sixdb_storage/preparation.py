"""Disposable per-key dataset stages and Linux closure lifetime protection.

Only a stage carrying its own inode-bound marker is disposable. Callers hold
the real dataset key lock for creation, execution, publication and removal.
The fork supervisor inherits that lock and reaps private PID 1 before releasing
it, including when the calling manager is killed.
"""
from contextlib import contextmanager
import ctypes
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import stat
import sys
import threading

PREFIX = '.pending-'
KEY = re.compile(r'[0-9a-f]{64}')
MAX_RESULT = 65536
DIRECTORY = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC


def _inode(descriptor):
    # ext4's UUID-derived f_fsid survives a loop minor changing on remount.
    return [os.fstatvfs(descriptor).f_fsid, os.fstat(descriptor).st_ino]


def _owned(parent, descriptor, key):
    fd = os.open(PREFIX + key + '.json', os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
    with os.fdopen(fd, 'rb') as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > 4096:
            raise ValueError('invalid dataset pending marker')
        marker = _inode(source.fileno())
        value = json.loads(source.read(4097))
    if not isinstance(value, dict):
        raise ValueError('invalid dataset pending marker')
    expected = {'format': 1, 'key': key, 'parent': _inode(parent),
                'directory': value.get('directory') if descriptor is None else _inode(descriptor),
                'marker': marker}
    if (value != expected or info.st_uid != os.geteuid() or
            descriptor is not None and os.fstat(descriptor).st_uid != os.geteuid()):
        raise ValueError('dataset pending ownership differs; preserved')


def reclaim(directory, key):
    """Remove this exact owned stage, under its key lock; preserve unknown roots."""
    if not isinstance(key, str) or not KEY.fullmatch(key):
        raise ValueError('invalid dataset stage key')
    name = PREFIX + key
    with _directory(directory) as parent:
        try:
            descriptor = os.open(name, DIRECTORY, dir_fd=parent)
        except FileNotFoundError:
            try:
                _owned(parent, None, key)
            except FileNotFoundError:
                return
            except ValueError:
                if not _empty_marker(parent, name + '.json'):
                    raise
            os.unlink(name + '.json', dir_fd=parent)
            os.fsync(parent)
            return
        try:
            try:
                _owned(parent, descriptor, key)
            except (OSError, ValueError, TypeError) as exc:
                # A killed mkdir/marker writer has not started a producer. Only
                # an empty private directory and absent/empty reserved marker
                # are safe to discard without a complete ownership record.
                info = os.fstat(descriptor)
                if (info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) != 0o700 or
                        os.listdir(descriptor) or not _empty_marker(parent, name + '.json')):
                    raise ValueError(f'Unknown/substituted dataset stage preserved: {Path(directory) / name}') from exc
            # Work relative to the held inode. Never traverse a substituted root
            # or follow a link out of the declared disposable stage.
            for entry in os.listdir(descriptor):
                info = os.stat(entry, dir_fd=descriptor, follow_symlinks=False)
                if stat.S_ISDIR(info.st_mode):
                    shutil.rmtree(entry, dir_fd=descriptor)
                else:
                    os.unlink(entry, dir_fd=descriptor)
            current, held = os.stat(name, dir_fd=parent, follow_symlinks=False), os.fstat(descriptor)
            if (current.st_dev, current.st_ino) != (held.st_dev, held.st_ino):
                raise ValueError('dataset pending root substituted; preserved')
            os.rmdir(name, dir_fd=parent)
            try:
                os.unlink(name + '.json', dir_fd=parent)
            except FileNotFoundError:
                pass
            os.fsync(parent)
        finally:
            os.close(descriptor)


def _empty_marker(parent, name):
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
    except FileNotFoundError:
        return True
    with os.fdopen(fd, 'rb') as source:
        info = os.fstat(source.fileno())
        return (stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_size == 0 and
                info.st_uid == os.geteuid() and stat.S_IMODE(info.st_mode) == 0o600)


@contextmanager
def _directory(path):
    descriptor = os.open(path, DIRECTORY)
    try:
        yield descriptor
    finally:
        os.close(descriptor)


def reconcile(directory, lock):
    """Reclaim idle keys, taking each actual key lock without blocking peers."""
    with _directory(directory) as parent:
        names = os.listdir(parent)
    for name in names:
        key = name.removeprefix(PREFIX).removesuffix('.json')
        if name.startswith(PREFIX) and KEY.fullmatch(key):
            with lock(key, blocking=False) as acquired:
                if acquired:
                    reclaim(directory, key)


@contextmanager
def stage(directory, key):
    """One deterministic, owned pending root; never touch accepted cache data."""
    if not isinstance(key, str) or not KEY.fullmatch(key):
        raise ValueError('invalid dataset stage key')
    reclaim(directory, key)
    name = PREFIX + key
    with _directory(directory) as parent:
        os.mkdir(name, mode=0o700, dir_fd=parent)
        descriptor = os.open(name, DIRECTORY, dir_fd=parent)
        try:
            fd = os.open(name + '.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=parent)
            with os.fdopen(fd, 'w') as target:
                value = {'format': 1, 'key': key, 'parent': _inode(parent),
                         'directory': _inode(descriptor), 'marker': _inode(target.fileno())}
                target.write(json.dumps(value, sort_keys=True) + '\n')
                target.flush()
                os.fsync(target.fileno())
            os.fsync(descriptor)
            os.fsync(parent)
        finally:
            os.close(descriptor)
    try:
        yield Path(directory) / name
    finally:
        reclaim(directory, key)


def _send(fd, value):
    data = json.dumps(value, separators=(',', ':'), ensure_ascii=True).encode()
    if len(data) > MAX_RESULT:
        data = b'{"error":"ValueError","message":"dataset callback result exceeds 64 KiB"}'
    while data:
        data = data[os.write(fd, data):]


def _supervise(callback, output, manager):
    """Wait outside the private PID namespace, retaining inherited key locks."""
    child, stopped = None, False
    def stop(*_):
        nonlocal stopped
        stopped = True
        if child is not None:
            try:
                os.kill(child, signal.SIGKILL)
            except ProcessLookupError:
                pass
    signal.signal(signal.SIGTERM, stop)
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGTERM, 0, 0, 0) != 0 or os.getppid() != manager:
        os._exit(125)
    try:
        uid, gid = os.getuid(), os.getgid()
        if libc.unshare(0x10000000) != 0:  # CLONE_NEWUSER
            raise OSError(ctypes.get_errno(), 'dataset preparation needs Linux user/PID namespaces')
        Path('/proc/self/uid_map').write_text(f'0 {uid} 1\n')
        Path('/proc/self/setgroups').write_text('deny\n')
        Path('/proc/self/gid_map').write_text(f'0 {gid} 1\n')
        if libc.unshare(0x20000000) != 0:  # CLONE_NEWPID: next child becomes PID 1.
            raise OSError(ctypes.get_errno(), 'dataset preparation needs Linux PID namespaces')
        # PID 1 sees its outside parent as PID 0. A pidfd closes the tiny
        # fork/prctl race without relying on a host /proc PID interpretation.
        parent_fd = os.pidfd_open(os.getpid())
        child = os.fork()
        if child == 0:
            signal.signal(signal.SIGTERM, signal.SIG_DFL)
            if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or select.select([parent_fd], [], [], 0)[0]:
                os._exit(125)
            os.close(parent_fd)
            try:
                _send(output, {'result': callback()})
            except BaseException as exc:
                _send(output, {'error': type(exc).__name__, 'message': str(exc)[:8192]})
            finally:
                os.close(output)
                # Linux kills and reaps the entire namespace as PID 1 exits,
                # including descendants that setsid() or close every inherited FD.
                os._exit(0)
        os.close(parent_fd)
        os.close(output)
        if stopped:
            stop()
        while True:
            try:
                os.waitpid(child, 0)
                break
            except InterruptedError:
                continue
    except BaseException as exc:
        _send(output, {'error': 'RuntimeError', 'message': str(exc)[:8192]})
    os._exit(0)


def run(callback):
    """Run an arbitrary closure; return bounded JSON only after descendants end."""
    if sys.platform != 'linux' or not hasattr(os, 'pidfd_open'):
        raise RuntimeError('dataset preparation requires Linux user/PID namespaces and pidfd support')
    if threading.active_count() != 1:
        raise RuntimeError('prepared-data publication needs a single-threaded process; use separate processes for concurrent callers')
    sys.stdout.flush()
    sys.stderr.flush()
    reader, writer = os.pipe2(os.O_CLOEXEC)
    manager = os.getpid()
    child = os.fork()
    if child == 0:
        os.close(reader)
        _supervise(callback, writer, manager)
    os.close(writer)
    data = bytearray()
    try:
        while True:
            chunk = os.read(reader, min(8192, MAX_RESULT + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
            if len(data) > MAX_RESULT:
                raise RuntimeError('dataset callback result exceeds 64 KiB')
    finally:
        os.close(reader)
        # A signal/exception in the manager must also finish the producer before
        # stage() considers cleanup. The supervisor waits for private PID 1.
        try:
            os.kill(child, signal.SIGTERM)
        except ProcessLookupError:
            pass
        _, status = os.waitpid(child, 0)
    if not data or status:
        raise RuntimeError('dataset preparation lifetime guard failed')
    result = json.loads(data)
    if 'error' in result:
        error = {'ValueError': ValueError, 'FileNotFoundError': FileNotFoundError,
                 'RuntimeError': RuntimeError}.get(result['error'], RuntimeError)
        raise error(result['message'])
    return result['result']
