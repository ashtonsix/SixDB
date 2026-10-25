"""Borrow immutable input views from the shared CAS; consumers run without its lock."""
from __future__ import annotations

import ctypes
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import time
import uuid

from .acquire import (MAX_REFERENCE, acquire, archive_identity, choose_members,
                      inspect_archive, manifest_digest, read_json,
                      reference_identity, validate_manifest)
from .store import MARGIN, Refused, Store, owner_alive, process_identity

MAX_RECEIPT = 16 * 1024**2
MAX_VIEWS = 64
MAX_ROOTS = 16


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def _future(store):
    return json.loads(json.dumps(store.catalog))


def _commit(store, future):
    store.catalog = future  # Removed dead/released pins may fund publication.
    store.commit(future)


def _owner(pid):
    identity = process_identity(pid)
    proc = Path('/proc') / str(identity['pid'])
    command = (proc / 'comm').read_text().strip().lower()
    if identity['pid'] == 1 or command in {'init', 'systemd', 'launchd', 'orbstack-agent', 'orb-agent'} or 'orbstack-agent' in command:
        raise Refused('persistent launcher cannot own an input borrow; use artifacts.py input --run -- COMMAND')
    if proc.stat().st_uid != os.getuid():
        raise Refused('borrower must belong to the calling user')
    return identity


def _view_root(store, value):
    root = Path(value or (store.root.parent / 'input-views')).absolute().resolve()
    if not root.resolve().is_relative_to(store.root.parent.resolve()) or root.resolve().is_relative_to(store.root.resolve()):
        raise Refused('input view metadata must be beside the shared CAS in the managed volume')
    roots = store.catalog.get('input_roots', [])
    if not isinstance(roots, list) or any(not isinstance(path, str) for path in roots):
        raise Refused('invalid input view roots')
    if str(root) not in roots:
        if len(roots) >= MAX_ROOTS:
            active = {owner['view'] for owner in store.catalog.get('generated_owners', {}).values()}
            views = store.catalog.get('input_views', {})
            for old in roots:
                entries = {key: entry for key, entry in views.items() if entry['root'] == old}
                if any(str(Path(old) / key) in active for key in entries):continue
                expected = {name for key in entries for name in (key, key + '.json')}
                if any(path.name not in expected for path in Path(old).iterdir()):
                    raise Refused('changed input metadata location; preserved')
                for key, entry in entries.items():
                    _remove(Path(old), key, key + '.json', store, entry['receipt'])
                    del store.catalog['input_views'][key]
                Path(old).rmdir()  # Atomic refusal if unknown material appeared.
                roots.remove(old)
                break
            else:
                raise Refused('input view location capacity is held by live borrowers')
        store.admit(8192 + MARGIN, external=True)
        if root.parent.stat().st_dev != store.root.stat().st_dev:
            raise Refused('input views and shared CAS must use the same filesystem')
        future = _future(store)
        future.setdefault('input_roots', []).append(str(root))
        _commit(store, future)
    # Ownership precedes directory creation, so a killed creator cannot leave
    # arbitrarily many unregistered empty view locations behind.
    root.mkdir(exist_ok=True)
    if root.stat().st_dev != store.root.stat().st_dev:
        raise Refused('input views and shared CAS must use the same filesystem')
    if root.is_symlink() or not root.is_dir():
        raise Refused('input view root changed; preserved')
    return root


def _receipt(root, name, expected):
    path = root / name
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > MAX_RECEIPT:
            raise Refused('input receipt type/size changed; preserved')
        raw = stream.read(MAX_RECEIPT + 1)
    if len(raw) > MAX_RECEIPT or _hash(raw) != expected:
        raise Refused('input receipt changed; preserved')
    data = json.loads(raw)
    if data.get('format') != 1:
        raise Refused('invalid input receipt')
    reference = reference_identity(data['artifact'])
    members = validate_manifest(data['members'], policy=True)
    chosen, _ = choose_members(reference, members, data['selected'], full=data['selected'] is None)
    return data, chosen


def _tree(path, chosen, store, *, partial=False):
    if path.is_symlink() or not path.is_dir():
        raise Refused('input view changed type; preserved')
    expected_dirs = {str(parent) for name in chosen for parent in Path(name).parents if str(parent) != '.'}
    found = set()
    for base, dirs, files in os.walk(path, followlinks=False):
        for name in dirs + files:
            item = Path(base) / name
            relative = item.relative_to(path).as_posix()
            info = item.lstat()
            if stat.S_ISDIR(info.st_mode) and relative in expected_dirs:
                continue
            if relative not in chosen or not stat.S_ISLNK(info.st_mode):
                raise Refused('unknown/changed input view material; preserved: ' + str(item))
            if os.readlink(item) != str(store.object_path(chosen[relative]['sha256'])):
                raise Refused('input view link changed; preserved: ' + str(item))
            found.add(relative)
    if not partial and found != set(chosen):
        raise Refused('input view membership changed; preserved')
    return found


def _charge(path):
    return sum(p.lstat().st_blocks * 512 + 512 for p in [path, *path.rglob('*')])


def _remove(root, view, receipt, store, expected):
    _, chosen = _receipt(root, receipt, expected)
    _tree(root / view, chosen, store)
    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        if not shutil.rmtree.avoids_symlink_attacks:
            raise Refused('safe directory deletion is unavailable')
        # Durable intent distinguishes interrupted tool retirement from a
        # researcher changing a published view. Rename cannot replace a path.
        retiring = '.retiring-' + view
        _rename(fd, view, retiring)
        os.fsync(fd)
        shutil.rmtree(retiring, dir_fd=fd)
        os.unlink(receipt, dir_fd=fd)
        os.fsync(fd)
    finally:
        os.close(fd)


def _retired(store):
    """Finish only recorded unleased retirements; never guess about edits."""
    views = store.catalog.get('input_views', {})
    active = {owner['view'] for owner in store.catalog.get('generated_owners', {}).values()}
    for key, entry in list(views.items()):
        root = _view_root(store, entry['root'])
        normal, retiring, receipt = root / key, root / ('.retiring-' + key), root / (key + '.json')
        present = lambda path: path.exists() or path.is_symlink()
        if present(normal):
            if present(retiring):raise Refused('ambiguous input retirement; preserved')
            continue
        if str(normal) in active:
            raise Refused('active input view disappeared; preserved')
        if present(retiring):
            _, chosen = _receipt(root, key + '.json', entry['receipt'])
            _tree(retiring, chosen, store, partial=True)
        elif not present(receipt):
            # The last filesystem unlink can precede catalog publication.
            del views[key]
            continue
        else:
            _receipt(root, key + '.json', entry['receipt'])
        fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            if present(retiring):shutil.rmtree(retiring.name, dir_fd=fd)
            os.unlink(receipt.name, dir_fd=fd)
            os.fsync(fd)
        finally:os.close(fd)
        del views[key]


def _pending(store):
    pending = store.catalog.get('input_pending')
    if pending is None:
        return
    root = _view_root(store, pending['root'])
    view, nonce, expected = pending['view'], pending['nonce'], pending['receipt']
    if (len(view) != 64 or any(c not in '0123456789abcdef' for c in view) or
            len(nonce) != 32 or any(c not in '0123456789abcdef' for c in nonce)):
        raise Refused('invalid input transaction; preserved')
    receipt_names = [view + '.json', '.receipt-' + nonce]
    present = lambda path: path.exists() or path.is_symlink()
    full = next((name for name in receipt_names if present(root / name)), None)
    directories = [name for name in (view, '.view-' + nonce) if present(root / name)]
    chosen = None
    if full is not None:
        try:
            _, chosen = _receipt(root, full, expected)
        except (Refused, ValueError):
            # A killed receipt writer predates view creation. Only its exact
            # private regular scratch file is disposable, never a changed view.
            if directories or full == view + '.json':
                raise
    if directories and chosen is None:
        raise Refused('input transaction lost verification receipt; preserved')
    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for name in directories:
            _tree(root / name, chosen, store, partial=name.startswith('.view-'))
            shutil.rmtree(name, dir_fd=fd)
        for name in receipt_names:
            try:
                info = os.stat(name, dir_fd=fd, follow_symlinks=False)
            except FileNotFoundError:
                continue
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise Refused('input scratch changed type; preserved')
            os.unlink(name, dir_fd=fd)
        os.fsync(fd)
    finally:
        os.close(fd)
    future = _future(store)
    token = future.pop('input_pending')['token']
    future['leases'].pop(token, None)
    future.get('pin_keys', {}).pop(token, None)
    future.get('generated_owners', {}).pop(token, None)
    _commit(store, future)


def _capacity(store, extra, *, keep=None, inodes=4):
    store.reap_generated(persist=False)
    views = store.catalog.get('input_views', {})
    if not isinstance(views, dict):
        raise Refused('invalid input view registry')
    limit = max(8 * 1024**2, min(64 * 1024**2, store.budget * 2))
    if extra > limit:
        raise Refused('requested input metadata exceeds the bounded view allowance')
    charges = {}
    for name in store.catalog.get('input_roots', []):
        root = _view_root(store, name)
        known = {part for key, entry in views.items() if entry['root'] == name
                 for part in (key, key + '.json')}
        if any(path.name not in known for path in root.iterdir()):
            raise Refused('unknown input metadata material; preserved: ' + str(root))
    for key, entry in views.items():
        root = _view_root(store, entry['root'])
        _, chosen = _receipt(root, key + '.json', entry['receipt'])
        _tree(root / key, chosen, store)
        charges[key] = _charge(root / key) + _charge(root / (key + '.json'))
    active = {owner['view'] for owner in store.catalog.get('generated_owners', {}).values()}
    def physical_short():
        import storage
        fs = os.statvfs(store.root)
        reserve = max(extra + MARGIN, len(Store.encode(store.catalog)) + MARGIN)
        free = storage.headroom(store.root)['destination']['free']
        return (free < reserve or
                (fs.f_files and fs.f_favail < inodes))
    try:
        store.admit(extra + MARGIN, external=True, inodes=inodes)
    except Refused:
        if not physical_short():
            raise
    changed = False
    while (sum(charges.values()) + extra > limit or
           len(views) + (keep not in views) > MAX_VIEWS or physical_short()):
        candidates = [key for key in views if key != keep and str(Path(views[key]['root']) / key) not in active]
        if not candidates:
            raise Refused('input metadata capacity refused: active/changed views preserved; use run-scoped consumers')
        key = min(candidates, key=lambda k: views[k]['touched'])
        entry = views[key]
        _remove(Path(entry['root']), key, key + '.json', store, entry['receipt'])
        future = _future(store)
        del future['input_views'][key]
        store.catalog = future
        changed = True
        views = store.catalog.get('input_views', {})
        del charges[key]
    if changed:_commit(store, store.catalog)


def _rename(fd, source, target):
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.renameat2(fd, os.fsencode(source), fd, os.fsencode(target), 1) != 0:
        value = ctypes.get_errno()
        raise OSError(value, os.strerror(value), target)


def _publish(store, root, key, receipt, token):
    data = Store.encode(receipt)
    if len(data) > MAX_RECEIPT:
        raise Refused('input manifest receipt exceeds its bounded allowance')
    chosen, _ = choose_members(receipt['artifact'], receipt['members'], receipt['selected'], full=receipt['selected'] is None)
    dirs = {str(p) for name in chosen for p in Path(name).parents if str(p) != '.'}
    allowance = ((len(data) + 4095) // 4096) * 4096 + (len(chosen) + len(dirs) + 1) * 4608
    _capacity(store, allowance, keep=key, inodes=len(chosen) + len(dirs) + 8)
    store.admit(allowance + MARGIN, inodes=len(chosen) + len(dirs) + 8, external=True)
    if any(path.exists() or path.is_symlink() for path in (root / key, root / (key + '.json'))):
        raise Refused('unknown input destination already exists; preserved')
    nonce, checksum = uuid.uuid4().hex, _hash(data)
    future = _future(store)
    future['input_pending'] = {'root': str(root), 'view': key, 'nonce': nonce, 'receipt': checksum, 'token': token}
    _commit(store, future)
    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        name = '.receipt-' + nonce
        out = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=fd)
        with os.fdopen(out, 'wb') as stream:
            stream.write(data);stream.flush();os.fsync(stream.fileno());os.fchmod(stream.fileno(), 0o444)
        stage = '.view-' + nonce
        os.mkdir(stage, 0o700, dir_fd=fd)
        for name, entry in chosen.items():
            parent = os.open(stage, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            try:
                parts = Path(name).parts
                for component in parts[:-1]:
                    try:os.mkdir(component, 0o700, dir_fd=parent)
                    except FileExistsError:pass
                    child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
                    os.close(parent);parent = child
                os.symlink(str(store.object_path(entry['sha256'])), parts[-1], dir_fd=parent)
            finally:
                os.close(parent)
        for base, _, _ in os.walk(root / stage, topdown=False):
            child = os.open(base, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:os.fsync(child)
            finally:os.close(child)
        _rename(fd, '.receipt-' + nonce, key + '.json')
        _rename(fd, stage, key)
        os.fsync(fd)
        _tree(root / key, chosen, store)
        future = _future(store)
        future.setdefault('input_views', {})[key] = {'root': str(root), 'receipt': checksum,
            'archive': archive_identity(receipt['artifact']), 'touched': time.time_ns()}
        if token in future.get('generated_owners', {}):future['generated_owners'][token]['acquiring'] = False
        future.pop('input_pending', None)
        _commit(store, future)
    finally:
        os.close(fd)


@contextmanager
def _manager():
    """An ended acquisition can expire even if a failed cancellation could not write."""
    # Exec closes other threads' Store locks and guardian pipes. A raw fork
    # inherits both and can create a cycle between concurrent acquisitions.
    command = ('import ctypes,os,signal,sys; '
               'parent=int(sys.argv[1]); '
               'libc=ctypes.CDLL(None,use_errno=True); '
               'bound=libc.prctl(1,signal.SIGKILL,0,0,0)==0; '
               'sys.exit(125) if not bound or os.getppid()!=parent else None; '
               'os.read(0,1)')
    child = subprocess.Popen([sys.executable, '-c', command, str(os.getpid())],
                             stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, close_fds=True)
    try:
        yield process_identity(child.pid)
    finally:
        if child.poll() is None:
            child.terminate()
        child.stdin.close()
        child.wait()


def _borrow(reference_path, cache_root, budget, selected, owner_pid, view_root):
    with _manager() as manager:
        return _borrow_owned(reference_path, cache_root, budget, selected, owner_pid, view_root, manager)


def _borrow_owned(reference_path, cache_root, budget, selected, owner_pid, view_root, manager):
    path = Path(reference_path)
    if path.is_dir():path = path / 'artifact.json'
    reference = reference_identity(read_json(path, MAX_REFERENCE))
    selected = sorted(set(selected)) if selected is not None else None
    identity = _owner(os.getpid() if owner_pid is None else owner_pid)
    store = Store(cache_root, budget)
    token = 'input-' + uuid.uuid4().hex
    try:
        _pending(store)
        _retired(store)
        root = _view_root(store, view_root)
        key = _hash(Store.encode({'root': str(root), 'artifact': reference, 'selected': selected}))
        destination = root / key
        entry = store.catalog.get('input_views', {}).get(key)
        if entry is None:
            manifest = None
            for other_key, other in store.catalog.get('input_views', {}).items():
                if other.get('archive') == archive_identity(reference):
                    manifest, _ = _receipt(Path(other['root']), other_key + '.json', other['receipt'])
                    break
            if manifest is None:manifest = inspect_archive(reference, store=store)
        else:
            manifest, _ = _receipt(root, key + '.json', entry['receipt'])
            chosen = choose_members(reference, manifest['members'], selected, full=selected is None)[0]
            _tree(destination, chosen, store)
        # Checked owned receipts serve other selections/locations too. Their
        # authenticated policy can refresh the bounded hint LRU without I/O.
        hint, proof = archive_identity(reference), manifest_digest(manifest['members'])
        if store.catalog.get('archive_hints', {}).get(hint, {}).get('sha256') != proof:
            chosen = choose_members(reference, manifest['members'], selected, full=selected is None)[0]
            store.consumer_objects.update(item['sha256'] for item in chosen.values())
            store.remember_archive_hint(hint, proof)
        identity['view'] = str(destination)
        identity['manager'] = manager
        identity['acquiring'] = True
        store.generated_registration[token] = identity
        result = acquire(store, reference, selected=selected, full=selected is None,
                         expected={'artifact': reference, 'members': manifest['members']}, pin=token)
        if entry is None:
            _publish(store, root, key, {'format': 1, 'artifact': reference,
                     'members': manifest['members'], 'selected': selected}, token)
        else:
            _capacity(store, 0, keep=key)
            future = _future(store)
            future['input_views'][key]['touched'] = time.time_ns()
            if token in future.get('generated_owners', {}):future['generated_owners'][token]['acquiring'] = False
            _commit(store, future)
        if not owner_alive(identity) or not owner_alive(manager):
            raise Refused('borrower/acquisition ended before input publication completed')
        return destination, token
    except BaseException:
        try:
            _pending(store)
            store.release(token)
        except (OSError, Refused, ValueError):
            pass
        raise
    finally:
        store.close()


def borrow(reference_path, cache_root, budget, selected=None, owner_pid=None, *, view_root=None):
    """Return shared read-only member paths, pinned until this borrower dies.

    A CLI must pass its real caller PID explicitly. Persistent launcher parents
    refuse; use run() rather than pinning to a long-lived service accidentally.
    """
    return _borrow(reference_path, cache_root, budget, selected, owner_pid, view_root)[0]


def run(reference_path, cache_root, budget, command, selected=None, *, view_root=None):
    """Own a private PID-guarded consumer scope; never hold the CAS lock during it."""
    if not command:raise Refused('input run needs a command')
    destination, token = _borrow(reference_path, cache_root, budget, selected, os.getpid(), view_root)
    parent = os.getpid()
    libc = ctypes.CDLL(None, use_errno=True)
    def bind_parent_lifetime():
        if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0 or os.getppid() != parent:os._exit(125)
    try:
        env = dict(os.environ, SIXDB_INPUT_ROOT=str(destination))
        args = [str(arg).replace('{input}', str(destination)) for arg in command]
        if not args:raise Refused('input run needs a command')
        child = subprocess.run(['unshare','--user','--map-root-user','--pid','--fork',
                                '--kill-child=SIGKILL','--',*args],
                               env=env, preexec_fn=bind_parent_lifetime)
        return child.returncode if child.returncode >= 0 else 128 - child.returncode
    finally:
        store = Store(cache_root, budget)
        try:
            future = _future(store)
            future['leases'].pop(token, None)
            future.get('pin_keys', {}).pop(token, None)
            future.get('generated_owners', {}).pop(token, None)
            store.catalog = future
            _retired(store)
            _capacity(store, 0)
            _commit(store, store.catalog)
        finally:store.close()
